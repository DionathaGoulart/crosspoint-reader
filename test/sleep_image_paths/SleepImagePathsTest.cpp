#include <ProfilePath.h>
#include <gtest/gtest.h>

#include "util/SleepImagePaths.h"

namespace {
struct ActiveProfile {
  explicit ActiveProfile(uint8_t id) { profilePath::setActiveId(id); }
  ~ActiveProfile() { profilePath::setActiveId(profilePath::DEFAULT_ID); }
};
}  // namespace

TEST(SleepImagePaths, DefaultProfileKeepsTheSdRoot) {
  ActiveProfile profile(profilePath::DEFAULT_ID);
  EXPECT_EQ(sleepimages::rootDir(), "/");
  EXPECT_EQ(sleepimages::path(".sleep"), "/.sleep");
  EXPECT_EQ(sleepimages::path("sleep.bmp"), "/sleep.bmp");
  EXPECT_EQ(sleepimages::path("sleep-overlay.png"), "/sleep-overlay.png");
}

TEST(SleepImagePaths, OtherProfilesHaveTheirOwnFolder) {
  ActiveProfile profile(3);
  EXPECT_EQ(sleepimages::rootDir(), "/.crosspoint/profiles/3/");
  EXPECT_EQ(sleepimages::path(".sleep"), "/.crosspoint/profiles/3/.sleep");
  EXPECT_EQ(sleepimages::path("sleep.bmp"), "/.crosspoint/profiles/3/sleep.bmp");
}

TEST(SleepImagePaths, RecognisesSleepImagesAtTheRootAndInProfiles) {
  using sleepimages::isSleepImagePath;
  EXPECT_TRUE(isSleepImagePath("/sleep.bmp"));
  EXPECT_TRUE(isSleepImagePath("//sleep.bmp"));
  EXPECT_TRUE(isSleepImagePath("/SLEEP.BMP"));
  EXPECT_TRUE(isSleepImagePath("/.sleep/a.bmp"));
  EXPECT_TRUE(isSleepImagePath("/sleep/a.bmp"));
  EXPECT_TRUE(isSleepImagePath("/.crosspoint/profiles/2/sleep.bmp"));
  EXPECT_TRUE(isSleepImagePath("/.crosspoint/profiles/12/.sleep/a.bmp"));
  EXPECT_TRUE(isSleepImagePath("/.crosspoint/profiles/2/sleep/a.bmp"));
}

TEST(SleepImagePaths, IgnoresOtherFiles) {
  using sleepimages::isSleepImagePath;
  EXPECT_FALSE(isSleepImagePath(nullptr));
  EXPECT_FALSE(isSleepImagePath("/books/sleep.bmp"));
  EXPECT_FALSE(isSleepImagePath("/sleep.bmp.tmp"));
  EXPECT_FALSE(isSleepImagePath("/.crosspoint/profiles/x/sleep.bmp"));
  EXPECT_FALSE(isSleepImagePath("/.crosspoint/profiles//sleep.bmp"));
  EXPECT_FALSE(isSleepImagePath("/.crosspoint/profiles/2/recent.json"));
  EXPECT_FALSE(isSleepImagePath("/.crosspoint/sleep.bmp"));
}
