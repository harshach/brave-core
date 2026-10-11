// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_LATER_SERVICE_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_LATER_SERVICE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "base/timer/wall_clock_timer.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

// Pages and reminders the user has put off until later, after SigmaOS's
// snoozed tabs and reminders. Both live in profile prefs so they survive a
// restart. A snoozed page is closed when it is snoozed and reopened in its
// Space, in the profile's most recently used window, once its time comes. A
// reminder stays listed until it is marked done; once due it is shown until it
// is done or dismissed.
class OriginLaterService : public KeyedService {
 public:
  struct SnoozedPage {
    std::string id;
    GURL url;
    std::u16string title;
    std::string space_id;
    base::Time wake_time;
  };

  struct Reminder {
    std::string id;
    std::u16string title;
    // The page the reminder is about, if any.
    GURL url;
    std::string space_id;
    base::Time due_time;
    bool done = false;
    // Hidden from the sidebar's due notice, still listed in the panel.
    bool dismissed = false;
  };

  class Observer : public base::CheckedObserver {
   public:
    // The snoozed pages or reminders changed, or a reminder came due.
    virtual void OnOriginLaterChanged() {}
    // A snoozed page is back, as `contents` in one of the profile's windows.
    virtual void OnSnoozedPageReturned(content::WebContents* contents) {}
  };

  explicit OriginLaterService(Profile* profile);
  OriginLaterService(const OriginLaterService&) = delete;
  OriginLaterService& operator=(const OriginLaterService&) = delete;
  ~OriginLaterService() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Records a page to reopen at `wake_time`. The caller closes the page.
  void SnoozePage(const GURL& url,
                  std::u16string title,
                  std::string space_id,
                  base::Time wake_time);
  // Snoozes `contents` once it has closed; the caller closes it. A page kept
  // open from its "Leave site?" prompt isn't snoozed. False, and nothing
  // recorded, for a page that couldn't be reopened, which should stay open.
  bool SnoozeWhenClosed(content::WebContents* contents,
                        std::string space_id,
                        base::Time wake_time);
  // Soonest first.
  const std::vector<SnoozedPage>& snoozed_pages() const { return snoozed_; }
  void WakeNow(const std::string& id);
  void RemoveSnoozedPage(const std::string& id);

  std::string AddReminder(std::u16string title,
                          const GURL& url,
                          std::string space_id,
                          base::Time due_time);
  // Open reminders soonest first, then done ones in due order.
  const std::vector<Reminder>& reminders() const { return reminders_; }
  void SetReminderDone(const std::string& id, bool done);
  void DismissReminder(const std::string& id);
  void RemoveReminder(const std::string& id);
  // Due, not done reminders.
  int GetDueReminderCount() const;
  // The longest-waiting due reminder the user hasn't dismissed or done.
  std::optional<Reminder> GetReminderToShow() const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  void SetNowForTesting(base::Time now) { now_for_testing_ = now; }

 private:
  base::Time Now() const;
  void Load();
  void Save();
  void SortAndNotify();
  // Arms the timer for the next snoozed page or reminder.
  void ScheduleNext();
  void OnTimer();
  // False when the profile has no window to open into yet.
  bool OpenSnoozedPage(const SnoozedPage& page);

  raw_ptr<Profile> profile_ = nullptr;
  std::vector<SnoozedPage> snoozed_;
  std::vector<Reminder> reminders_;
  base::WallClockTimer timer_;
  std::optional<base::Time> now_for_testing_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<OriginLaterService> weak_factory_{this};
};

class OriginLaterServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static OriginLaterServiceFactory* GetInstance();
  // Null for off-the-record profiles: private pages are never kept for later.
  static OriginLaterService* GetForProfile(Profile* profile);

  OriginLaterServiceFactory(const OriginLaterServiceFactory&) = delete;
  OriginLaterServiceFactory& operator=(const OriginLaterServiceFactory&) =
      delete;

 private:
  friend class base::NoDestructor<OriginLaterServiceFactory>;

  OriginLaterServiceFactory();
  ~OriginLaterServiceFactory() override;

  // ProfileKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
  // Snoozed pages wake at their time even before a window asks.
  bool ServiceIsCreatedWithBrowserContext() const override;
};

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_LATER_SERVICE_H_
