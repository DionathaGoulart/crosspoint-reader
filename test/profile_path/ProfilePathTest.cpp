// Path composition for per-profile files.
//
// This is where a quiet mistake is expensive: a wrong path does not crash, it
// reads a file that is not there, so a reader would see their statistics reset
// rather than an error. The default profile keeping the historical names is
// what lets an existing card stay untouched, so that case is asserted as
// carefully as the profile ones.
#include <ProfilePath.h>

#include <gtest/gtest.h>

namespace {

class ProfilePathTest : public ::testing::Test {
 protected:
  // The active id is process-global, so each test states the profile it means
  // and puts it back rather than inheriting whatever ran before.
  void TearDown() override { profilePath::setActiveId(profilePath::DEFAULT_ID); }
};

TEST_F(ProfilePathTest, DefaultProfileKeepsTheHistoricalNames) {
  profilePath::setActiveId(profilePath::DEFAULT_ID);
  EXPECT_EQ(profilePath::resolve("recent.json", true), "/.crosspoint/recent.json");
  EXPECT_EQ(profilePath::activeDir(true), "/.crosspoint/");
}

TEST_F(ProfilePathTest, AbsoluteInputIsNotDoubled) {
  // Stores hand over the absolute path they have always used, which is what
  // keeps a per-profile store's change down to the one line that opts in.
  profilePath::setActiveId(profilePath::DEFAULT_ID);
  EXPECT_EQ(profilePath::resolve("/.crosspoint/recent.json", true), "/.crosspoint/recent.json");
  profilePath::setActiveId(1);
  EXPECT_EQ(profilePath::resolve("/.crosspoint/recent.json", true), "/.crosspoint/profiles/1/recent.json");
}

TEST_F(ProfilePathTest, ProfileGetsItsOwnDirectory) {
  profilePath::setActiveId(1);
  EXPECT_EQ(profilePath::resolve("recent.json", true), "/.crosspoint/profiles/1/recent.json");
  EXPECT_EQ(profilePath::activeDir(true), "/.crosspoint/profiles/1/");
}

TEST_F(ProfilePathTest, DeviceFileIgnoresTheActiveProfile) {
  // This is what keeps Wi-Fi and the LCP key from vanishing on a switch.
  profilePath::setActiveId(3);
  EXPECT_EQ(profilePath::resolve("wifi.json", false), "/.crosspoint/wifi.json");
  EXPECT_EQ(profilePath::resolve("/.crosspoint/wifi.json", false), "/.crosspoint/wifi.json");
  EXPECT_EQ(profilePath::activeDir(false), "/.crosspoint/");
}

TEST_F(ProfilePathTest, DirectoryNameKeepsItsTrailingSlash) {
  // Callers append a file name to it.
  profilePath::setActiveId(1);
  EXPECT_EQ(profilePath::resolve("bookmarks/", true), "/.crosspoint/profiles/1/bookmarks/");
}

TEST_F(ProfilePathTest, TwoDigitIdIsNotTruncated) {
  profilePath::setActiveId(12);
  EXPECT_EQ(profilePath::resolve("stats.bin", true), "/.crosspoint/profiles/12/stats.bin");
}

TEST_F(ProfilePathTest, NullNameIsNotDereferenced) {
  profilePath::setActiveId(1);
  EXPECT_EQ(profilePath::resolve(nullptr, true), "");
}

TEST_F(ProfilePathTest, ActiveIdRoundTrips) {
  profilePath::setActiveId(7);
  EXPECT_EQ(profilePath::activeId(), 7);
}

}  // namespace
