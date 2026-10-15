// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_later_service.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace {

constexpr char kSnoozedPagesPref[] = "brave.origin.snoozed_pages";
constexpr char kRemindersPref[] = "brave.origin.reminders";

constexpr char kIdKey[] = "id";
constexpr char kUrlKey[] = "url";
constexpr char kTitleKey[] = "title";
constexpr char kSpaceIdKey[] = "space_id";
constexpr char kTimeKey[] = "time";
constexpr char kDoneKey[] = "done";
constexpr char kDismissedKey[] = "dismissed";
constexpr char kRenamedKey[] = "renamed";

// A page can't wake while there is no window to wake into, which is how every
// session starts. Try again shortly rather than dropping it.
constexpr base::TimeDelta kRetryWithoutWindow = base::Seconds(5);

// Holds a snooze until its page has closed. Closing runs the page's
// beforeunload handler, and a page the user keeps must not come back later as
// a second copy.
class PendingSnooze : public content::WebContentsUserData<PendingSnooze>,
                      public content::WebContentsObserver {
 public:
  ~PendingSnooze() override = default;

  // content::WebContentsObserver:
  void BeforeUnloadFired(bool proceed) override {
    if (!proceed) {
      // Destroys `this`.
      web_contents()->RemoveUserData(UserDataKey());
    }
  }

  void WebContentsDestroyed() override {
    if (service_) {
      service_->SnoozePage(std::move(page_));
    }
    service_ = nullptr;
  }

 private:
  friend class content::WebContentsUserData<PendingSnooze>;

  PendingSnooze(content::WebContents* contents,
                base::WeakPtr<OriginLaterService> service,
                std::string space_id,
                base::Time wake_time)
      : content::WebContentsUserData<PendingSnooze>(*contents),
        content::WebContentsObserver(contents),
        service_(std::move(service)) {
    // Listed, and later reopened, under the name the page list shows.
    const std::u16string* custom_title =
        OriginSpaceController::GetCustomTitle(contents);
    page_ = {.url = contents->GetLastCommittedURL(),
             .title = custom_title ? *custom_title : contents->GetTitle(),
             .space_id = std::move(space_id),
             .wake_time = wake_time,
             .renamed = custom_title != nullptr};
  }

  base::WeakPtr<OriginLaterService> service_;
  OriginLaterService::SnoozedPage page_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PendingSnooze);

}  // namespace

OriginLaterService::OriginLaterService(Profile* profile) : profile_(profile) {
  CHECK(profile_);
  Load();
  ScheduleNext();
}

OriginLaterService::~OriginLaterService() = default;

// static
void OriginLaterService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kSnoozedPagesPref);
  registry->RegisterListPref(kRemindersPref);
}

void OriginLaterService::SnoozePage(SnoozedPage page) {
  if (!page.url.is_valid()) {
    return;
  }
  page.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  snoozed_.push_back(std::move(page));
  SortAndNotify();
}

bool OriginLaterService::SnoozeWhenClosed(content::WebContents* contents,
                                          std::string space_id,
                                          base::Time wake_time) {
  if (!contents || !contents->GetLastCommittedURL().is_valid()) {
    return false;
  }
  // Snoozing again before the page has closed replaces the earlier time.
  contents->RemoveUserData(PendingSnooze::UserDataKey());
  PendingSnooze::CreateForWebContents(contents, weak_factory_.GetWeakPtr(),
                                      std::move(space_id), wake_time);
  return true;
}

void OriginLaterService::WakeNow(const std::string& id) {
  auto it = std::ranges::find(snoozed_, id, &SnoozedPage::id);
  if (it == snoozed_.end()) {
    return;
  }
  const SnoozedPage page = *it;
  if (!OpenSnoozedPage(page)) {
    return;
  }
  std::erase_if(snoozed_, [&](const SnoozedPage& p) { return p.id == id; });
  SortAndNotify();
}

void OriginLaterService::RemoveSnoozedPage(const std::string& id) {
  if (std::erase_if(snoozed_,
                    [&](const SnoozedPage& page) { return page.id == id; })) {
    SortAndNotify();
  }
}

std::string OriginLaterService::AddReminder(std::u16string title,
                                            const GURL& url,
                                            std::string space_id,
                                            base::Time due_time) {
  std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  reminders_.push_back({id, std::move(title), url, std::move(space_id),
                        due_time, /*done=*/false, /*dismissed=*/false});
  SortAndNotify();
  return id;
}

void OriginLaterService::SetReminderDone(const std::string& id, bool done) {
  auto it = std::ranges::find(reminders_, id, &Reminder::id);
  if (it == reminders_.end() || it->done == done) {
    return;
  }
  it->done = done;
  SortAndNotify();
}

void OriginLaterService::DismissReminder(const std::string& id) {
  auto it = std::ranges::find(reminders_, id, &Reminder::id);
  if (it == reminders_.end() || it->dismissed) {
    return;
  }
  it->dismissed = true;
  SortAndNotify();
}

void OriginLaterService::RemoveReminder(const std::string& id) {
  if (std::erase_if(reminders_, [&](const Reminder& reminder) {
        return reminder.id == id;
      })) {
    SortAndNotify();
  }
}

int OriginLaterService::GetDueReminderCount() const {
  const base::Time now = Now();
  return std::ranges::count_if(reminders_, [&](const Reminder& reminder) {
    return !reminder.done && reminder.due_time <= now;
  });
}

std::optional<OriginLaterService::Reminder>
OriginLaterService::GetReminderToShow() const {
  const base::Time now = Now();
  // Sorted soonest first, so the first match has waited longest.
  for (const Reminder& reminder : reminders_) {
    if (!reminder.done && !reminder.dismissed && reminder.due_time <= now) {
      return reminder;
    }
  }
  return std::nullopt;
}

void OriginLaterService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void OriginLaterService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

base::Time OriginLaterService::Now() const {
  return now_for_testing_.value_or(base::Time::Now());
}

void OriginLaterService::Load() {
  PrefService* prefs = profile_->GetPrefs();
  for (const base::Value& value : prefs->GetList(kSnoozedPagesPref)) {
    const base::DictValue* dict = value.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString(kIdKey);
    const std::string* url = dict->FindString(kUrlKey);
    const std::string* title = dict->FindString(kTitleKey);
    const std::string* space_id = dict->FindString(kSpaceIdKey);
    const std::optional<base::Time> time =
        base::ValueToTime(dict->Find(kTimeKey));
    if (!id || !url || !GURL(*url).is_valid() || !time) {
      continue;
    }
    snoozed_.push_back({*id, GURL(*url),
                        title ? base::UTF8ToUTF16(*title) : std::u16string(),
                        space_id ? *space_id : std::string(), *time,
                        dict->FindBool(kRenamedKey).value_or(false)});
  }
  for (const base::Value& value : prefs->GetList(kRemindersPref)) {
    const base::DictValue* dict = value.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString(kIdKey);
    const std::string* title = dict->FindString(kTitleKey);
    const std::string* url = dict->FindString(kUrlKey);
    const std::string* space_id = dict->FindString(kSpaceIdKey);
    const std::optional<base::Time> time =
        base::ValueToTime(dict->Find(kTimeKey));
    if (!id || !title || !time) {
      continue;
    }
    reminders_.push_back({*id, base::UTF8ToUTF16(*title),
                          url ? GURL(*url) : GURL(),
                          space_id ? *space_id : std::string(), *time,
                          dict->FindBool(kDoneKey).value_or(false),
                          dict->FindBool(kDismissedKey).value_or(false)});
  }
  std::ranges::sort(snoozed_, {}, &SnoozedPage::wake_time);
  std::ranges::stable_sort(
      reminders_, [](const Reminder& a, const Reminder& b) {
        return std::tie(a.done, a.due_time) < std::tie(b.done, b.due_time);
      });
}

void OriginLaterService::Save() {
  base::ListValue snoozed;
  for (const SnoozedPage& page : snoozed_) {
    snoozed.Append(base::DictValue()
                       .Set(kIdKey, page.id)
                       .Set(kUrlKey, page.url.spec())
                       .Set(kTitleKey, base::UTF16ToUTF8(page.title))
                       .Set(kSpaceIdKey, page.space_id)
                       .Set(kTimeKey, base::TimeToValue(page.wake_time))
                       .Set(kRenamedKey, page.renamed));
  }
  profile_->GetPrefs()->SetList(kSnoozedPagesPref, std::move(snoozed));

  base::ListValue reminders;
  for (const Reminder& reminder : reminders_) {
    reminders.Append(base::DictValue()
                         .Set(kIdKey, reminder.id)
                         .Set(kTitleKey, base::UTF16ToUTF8(reminder.title))
                         .Set(kUrlKey, reminder.url.is_valid()
                                           ? reminder.url.spec()
                                           : std::string())
                         .Set(kSpaceIdKey, reminder.space_id)
                         .Set(kTimeKey, base::TimeToValue(reminder.due_time))
                         .Set(kDoneKey, reminder.done)
                         .Set(kDismissedKey, reminder.dismissed));
  }
  profile_->GetPrefs()->SetList(kRemindersPref, std::move(reminders));
}

void OriginLaterService::SortAndNotify() {
  std::ranges::sort(snoozed_, {}, &SnoozedPage::wake_time);
  // Open reminders by due time, then done ones.
  std::ranges::stable_sort(
      reminders_, [](const Reminder& a, const Reminder& b) {
        return std::tie(a.done, a.due_time) < std::tie(b.done, b.due_time);
      });
  Save();
  ScheduleNext();
  observers_.Notify(&Observer::OnOriginLaterChanged);
}

void OriginLaterService::ScheduleNext() {
  const base::Time now = Now();
  std::optional<base::Time> next;
  if (!snoozed_.empty()) {
    next = snoozed_.front().wake_time;
  }
  // A reminder needs the timer only to announce itself when it comes due.
  for (const Reminder& reminder : reminders_) {
    if (!reminder.done && reminder.due_time > now &&
        (!next || reminder.due_time < *next)) {
      next = reminder.due_time;
      break;
    }
  }
  if (!next) {
    timer_.Stop();
    return;
  }
  timer_.Start(
      FROM_HERE, std::max(*next, now),
      base::BindOnce(&OriginLaterService::OnTimer, base::Unretained(this)));
}

void OriginLaterService::OnTimer() {
  const base::Time now = Now();
  bool waiting_for_window = false;
  std::vector<SnoozedPage> due;
  for (const SnoozedPage& page : snoozed_) {
    if (page.wake_time <= now) {
      due.push_back(page);
    }
  }
  for (const SnoozedPage& page : due) {
    if (!OpenSnoozedPage(page)) {
      waiting_for_window = true;
      break;
    }
    std::erase_if(snoozed_,
                  [&](const SnoozedPage& p) { return p.id == page.id; });
  }
  Save();
  observers_.Notify(&Observer::OnOriginLaterChanged);
  if (waiting_for_window) {
    timer_.Start(
        FROM_HERE, now + kRetryWithoutWindow,
        base::BindOnce(&OriginLaterService::OnTimer, base::Unretained(this)));
    return;
  }
  ScheduleNext();
}

bool OriginLaterService::OpenSnoozedPage(const SnoozedPage& page) {
  // The window the user was in last. Activation order is exactly the point
  // here: the page comes back where the user is.
  BrowserWindowInterface* target = nullptr;
  ProfileBrowserCollection::GetForProfile(profile_)->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetType() == BrowserWindowInterface::Type::TYPE_NORMAL &&
            browser->GetFeatures().origin_space_controller()) {
          target = browser;
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  if (!target) {
    return false;
  }
  content::WebContents* contents =
      chrome::AddAndReturnTabAt(target, page.url, -1, /*foreground=*/false);
  if (!contents) {
    return false;
  }
  auto* space_controller = target->GetFeatures().origin_space_controller();
  if (!page.space_id.empty()) {
    space_controller->MoveTabToSpace(contents, page.space_id);
  }
  if (page.renamed) {
    space_controller->SetCustomTitle(contents, page.title);
  }
  observers_.Notify(&Observer::OnSnoozedPageReturned, contents);
  return true;
}

// static
OriginLaterServiceFactory* OriginLaterServiceFactory::GetInstance() {
  static base::NoDestructor<OriginLaterServiceFactory> instance;
  return instance.get();
}

// static
OriginLaterService* OriginLaterServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<OriginLaterService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

OriginLaterServiceFactory::OriginLaterServiceFactory()
    : ProfileKeyedServiceFactory("OriginLaterService",
                                 ProfileSelections::BuildForRegularProfile()) {}

OriginLaterServiceFactory::~OriginLaterServiceFactory() = default;

std::unique_ptr<KeyedService>
OriginLaterServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<OriginLaterService>(
      Profile::FromBrowserContext(context));
}

void OriginLaterServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  OriginLaterService::RegisterProfilePrefs(registry);
}

bool OriginLaterServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}
