// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/app/socket_profile_migration.h"

#import <AppKit/AppKit.h>

#include <optional>
#include <utility>

#include "base/apple/foundation_util.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"

namespace socket_profile {

namespace {

constexpr base::FilePath::CharType kLocalState[] =
    FILE_PATH_LITERAL("Local State");
constexpr base::FilePath::CharType kPreferences[] =
    FILE_PATH_LITERAL("Preferences");
constexpr base::FilePath::CharType kDefaultProfile[] =
    FILE_PATH_LITERAL("Default");

bool RemoveSingletonArtifacts(const base::FilePath& profile_dir) {
  base::FileEnumerator entries(profile_dir, false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    if (base::StartsWith(entry.BaseName().value(),
                         FILE_PATH_LITERAL("Singleton"),
                         base::CompareCase::SENSITIVE)) {
      if (!base::DeletePathRecursively(entry)) {
        return false;
      }
    }
  }
  return true;
}

bool IsBraveRunning() {
  return [NSRunningApplication
             runningApplicationsWithBundleIdentifier:@"com.brave.Browser"]
             .count > 0;
}

bool WaitForBraveToQuit() {
  while (IsBraveRunning()) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Quit Brave Before Importing";
    alert.informativeText =
        @"Socket copies the profile databases as a consistent snapshot. "
         "Quit Brave completely, then choose Check Again.";
    [alert addButtonWithTitle:@"Check Again"];
    [alert addButtonWithTitle:@"Quit Socket"];
    if ([alert runModal] != NSAlertFirstButtonReturn) {
      return false;
    }
  }
  return true;
}

std::optional<base::FilePath> ChooseBraveProfile(
    const base::FilePath& suggested_profile) {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.title = @"Choose Your Brave Browser Profile";
  panel.message =
      @"Choose the Brave-Browser folder. Socket will copy it and leave the "
       "original unchanged.";
  panel.prompt = @"Import Profile";
  panel.canChooseFiles = NO;
  panel.canChooseDirectories = YES;
  panel.allowsMultipleSelection = NO;
  panel.canCreateDirectories = NO;
  // Open on the profile itself rather than its parent: NSOpenPanel returns the
  // directory being shown when nothing is selected, and the parent has no
  // Local State, so clicking straight through used to be rejected.
  const base::FilePath start = base::PathExists(suggested_profile)
                                   ? suggested_profile
                                   : suggested_profile.DirName();
  panel.directoryURL =
      [NSURL fileURLWithPath:base::apple::FilePathToNSString(start)
                 isDirectory:YES];

  if ([panel runModal] != NSModalResponseOK || panel.URL == nil) {
    return std::nullopt;
  }
  return base::apple::NSStringToFilePath(panel.URL.path);
}

bool ShouldImportProfile() {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Bring Your Brave Profile to Socket";
  alert.informativeText =
      @"Socket can copy your profiles, signed-in sessions, tabs, history, "
       "passwords, extensions, settings, and Spaces. Brave's data remains "
       "unchanged. macOS may ask once for access to Brave's files and "
       "Keychain item.";
  [alert addButtonWithTitle:@"Import from Brave"];
  [alert addButtonWithTitle:@"Start Fresh"];
  return [alert runModal] == NSAlertFirstButtonReturn;
}

void ShowUnreadableProfileAlert() {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"Socket Can't Read That Folder";
  alert.informativeText =
      @"macOS is blocking access to another app's data. Grant Socket access "
       "when prompted, or allow it under Privacy & Security, then try again.";
  [alert addButtonWithTitle:@"OK"];
  [alert runModal];
}

void ShowInvalidProfileAlert() {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"That Isn't a Brave Profile";
  alert.informativeText =
      @"Choose the Brave-Browser folder that contains the Local State file.";
  [alert addButtonWithTitle:@"OK"];
  [alert runModal];
}

void ShowMigrationFailedAlert() {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleCritical;
  alert.messageText = @"Socket Couldn't Copy the Profile";
  alert.informativeText =
      @"Your Brave profile was not changed. Check that enough disk space is "
       "available, then open Socket and try again.";
  [alert addButtonWithTitle:@"Quit Socket"];
  [alert runModal];
}

bool CopyProfileWithProgress(const base::FilePath& source,
                             const base::FilePath& destination) {
  NSPanel* progress_window =
      [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 440, 132)
                                 styleMask:NSWindowStyleMaskTitled
                                   backing:NSBackingStoreBuffered
                                     defer:NO];
  progress_window.title = @"Socket";
  progress_window.releasedWhenClosed = NO;

  NSTextField* label = [NSTextField
      labelWithString:@"Copying your Brave profile… This can take a few "
                       "minutes."];
  label.frame = NSMakeRect(28, 77, 384, 22);
  label.alignment = NSTextAlignmentCenter;
  [progress_window.contentView addSubview:label];

  NSProgressIndicator* progress =
      [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(205, 30, 30, 30)];
  progress.style = NSProgressIndicatorStyleSpinning;
  [progress startAnimation:nil];
  [progress_window.contentView addSubview:progress];

  [progress_window center];
  [progress_window makeKeyAndOrderFront:nil];

  // This runs from PostEarlyInitialization, before the browser's ThreadPool
  // and main-thread task executor exist, so base::RunLoop and
  // PostTaskAndReplyWithResult are both unavailable here. Wait on an event and
  // pump AppKit by hand so the spinner keeps moving.
  bool succeeded = false;
  base::WaitableEvent done;
  base::Thread migration_thread("SocketProfileMigration");
  if (!migration_thread.Start() ||
      !migration_thread.task_runner()->PostTask(
          FROM_HERE, base::BindOnce(
                         [](base::FilePath source, base::FilePath destination,
                            bool* succeeded, base::WaitableEvent* done) {
                           *succeeded =
                               internal::CopyProfileData(source, destination);
                           done->Signal();
                         },
                         source, destination, &succeeded, &done))) {
    [progress_window orderOut:nil];
    return false;
  }

  while (!done.IsSignaled()) {
    @autoreleasepool {
      NSEvent* event = [NSApp
          nextEventMatchingMask:NSEventMaskAny
                      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                         inMode:NSDefaultRunLoopMode
                        dequeue:YES];
      if (event) {
        [NSApp sendEvent:event];
      }
    }
  }
  migration_thread.Stop();
  [progress_window orderOut:nil];
  return succeeded;
}

}  // namespace

namespace internal {

bool HasProfileData(const base::FilePath& profile_dir) {
  return base::PathExists(profile_dir.Append(kLocalState)) ||
         base::PathExists(
             profile_dir.Append(kDefaultProfile).Append(kPreferences));
}

bool CopyProfileData(const base::FilePath& source,
                     const base::FilePath& destination) {
  if (source.empty() || destination.empty() || source == destination ||
      source.IsParent(destination) || destination.IsParent(source) ||
      !base::PathExists(source.Append(kLocalState)) ||
      HasProfileData(destination)) {
    return false;
  }

  if (!base::CreateDirectory(destination.DirName())) {
    return false;
  }

  base::ScopedTempDir staging_root;
  if (!staging_root.CreateUniqueTempDirUnderPath(
          destination.DirName(), FILE_PATH_LITERAL(".socket-profile-import"))) {
    return false;
  }

  const base::FilePath staged_profile =
      staging_root.GetPath().Append(destination.BaseName());
  if (!base::CopyDirectory(source, staged_profile, true)) {
    return false;
  }
  if (!RemoveSingletonArtifacts(staged_profile) ||
      !base::PathExists(staged_profile.Append(kLocalState))) {
    return false;
  }

  base::FilePath previous_destination;
  if (base::DirectoryExists(destination)) {
    // Chromium creates scaffolding such as BrowserMetrics in the user data
    // directory before this runs, so the destination is never strictly empty.
    // HasProfileData above is what protects a real profile; requiring an empty
    // directory here only ever refused a valid import.
    previous_destination =
        staging_root.GetPath().Append(FILE_PATH_LITERAL("empty-destination"));
    if (!base::Move(destination, previous_destination)) {
      return false;
    }
  } else if (base::PathExists(destination)) {
    return false;
  }

  if (base::Move(staged_profile, destination)) {
    return true;
  }

  if (!previous_destination.empty() &&
      !base::Move(previous_destination, destination)) {
    LOG(ERROR) << "Failed to restore Socket's empty profile directory";
  }
  return false;
}

}  // namespace internal

StartupDisposition MaybeMigrateBraveProfile() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  // Once Socket has a profile the import is skipped forever, which makes a
  // failed first run unrecoverable and untestable. The switch forces the
  // prompt, including into a throwaway --user-data-dir, so the flow can be
  // exercised without touching the real profile. The copy itself still
  // refuses to overwrite existing data.
  const bool forced = command_line.HasSwitch("socket-force-profile-import");

  if (command_line.HasSwitch(switches::kUserDataDir) && !forced) {
    return StartupDisposition::kContinue;
  }

  base::FilePath destination;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &destination) ||
      (!forced && internal::HasProfileData(destination))) {
    return StartupDisposition::kContinue;
  }

  // A source given on the command line skips every prompt, so the import can
  // be driven end to end without a person clicking through modal panels.
  const base::FilePath supplied =
      command_line.GetSwitchValuePath("socket-profile-import-source");
  if (!supplied.empty()) {
    if (!base::PathExists(supplied.Append(kLocalState))) {
      LOG(ERROR) << "No Brave profile at " << supplied;
      return StartupDisposition::kExit;
    }
    // Deliberately the same wrapper the interactive path uses, so this
    // exercises the progress window and its waiting, not just the copy.
    const bool copied = CopyProfileWithProgress(supplied, destination);
    LOG(ERROR) << "Profile import " << (copied ? "succeeded" : "failed");
    return copied ? StartupDisposition::kContinue : StartupDisposition::kExit;
  }

  [NSApp activateIgnoringOtherApps:YES];
  if (!ShouldImportProfile()) {
    return StartupDisposition::kContinue;
  }
  if (!WaitForBraveToQuit()) {
    return StartupDisposition::kExit;
  }

  base::FilePath app_data;
  if (!base::PathService::Get(base::DIR_APP_DATA, &app_data)) {
    ShowMigrationFailedAlert();
    return StartupDisposition::kExit;
  }
  const base::FilePath suggested_profile =
      app_data.Append(FILE_PATH_LITERAL("BraveSoftware"))
          .Append(FILE_PATH_LITERAL("Brave-Browser"));

  std::optional<base::FilePath> source;
  while (!source) {
    source = ChooseBraveProfile(suggested_profile);
    if (!source) {
      return StartupDisposition::kExit;
    }
    if (!base::PathExists(source->Append(kLocalState))) {
      // A profile that exists but cannot be read is a permission problem, not
      // a wrong folder, and needs different advice.
      if (base::DirectoryExists(*source) && !base::IsDirectoryEmpty(*source)) {
        ShowUnreadableProfileAlert();
      } else {
        ShowInvalidProfileAlert();
      }
      source.reset();
    }
  }

  if (!CopyProfileWithProgress(*source, destination)) {
    ShowMigrationFailedAlert();
    return StartupDisposition::kExit;
  }

  return StartupDisposition::kContinue;
}

}  // namespace socket_profile
