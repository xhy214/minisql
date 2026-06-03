#include "buffer/clock_replacer.h"

#include "gtest/gtest.h"

TEST(CLOCKReplacerTest, SampleTest) {
  CLOCKReplacer clock_replacer(7);

  // Scenario: unpin six elements, i.e. add them to the replacer.
  clock_replacer.Unpin(1);
  clock_replacer.Unpin(2);
  clock_replacer.Unpin(3);
  clock_replacer.Unpin(4);
  clock_replacer.Unpin(5);
  clock_replacer.Unpin(6);
  EXPECT_EQ(6, clock_replacer.Size());

  // Scenario: get three victims from the clock.
  int value;
  clock_replacer.Victim(&value);
  EXPECT_EQ(1, value);
  clock_replacer.Victim(&value);
  EXPECT_EQ(2, value);
  clock_replacer.Victim(&value);
  EXPECT_EQ(3, value);

  // Scenario: pin elements in the replacer.
  // Note that 3 has already been victimized, so pinning 3 should have no effect.
  clock_replacer.Pin(3);
  clock_replacer.Pin(4);
  EXPECT_EQ(2, clock_replacer.Size());

  // Scenario: unpin 4. We expect the clock to add 4 with its reference bit set to 1.
  clock_replacer.Unpin(4);

  // Scenario: continue looking for victims. We expect these victims.
  clock_replacer.Victim(&value);
  EXPECT_EQ(5, value);
  clock_replacer.Victim(&value);
  EXPECT_EQ(6, value);
  clock_replacer.Victim(&value);
  EXPECT_EQ(4, value);
}

TEST(CLOCKReplacerTest, SecondChanceTest) {
  // This test demonstrates the clock algorithm's "second chance" behavior:
  // a page with its reference bit set to 1 gets a second chance and is not
  // victimized immediately.
  CLOCKReplacer clock_replacer(5);

  // Unpin three pages. All get reference bit = 1.
  clock_replacer.Unpin(1);
  clock_replacer.Unpin(2);
  clock_replacer.Unpin(3);
  EXPECT_EQ(3, clock_replacer.Size());

  // First victim scan: all ref bits are 1, so the hand sweeps through
  // all of them, clearing ref bits to 0. Page 1 is the final victim.
  int value;
  clock_replacer.Victim(&value);
  EXPECT_EQ(1, value);
  EXPECT_EQ(2, clock_replacer.Size());

  // Now pages 2 and 3 both have ref bit = 0.
  // Re-reference page 2 by unpinning it again. Its ref bit becomes 1.
  clock_replacer.Unpin(2);

  // Add page 4 with ref bit = 1.
  clock_replacer.Unpin(4);
  EXPECT_EQ(3, clock_replacer.Size());

  // Victim scan:
  // - Page 2 has ref=1 → cleared to 0, moved to back (second chance)
  // - Page 3 has ref=0 → victimized immediately
  // Page 2 survives even though it was added before page 3,
  // because its reference bit was refreshed via Unpin.
  clock_replacer.Victim(&value);
  EXPECT_EQ(3, value);
  EXPECT_EQ(2, clock_replacer.Size());

  // Remaining: [4, 2], ref[4]=1, ref[2]=0
  // - Page 4 has ref=1 → cleared to 0, moved to back (second chance)
  // - Page 2 has ref=0 → victimized
  clock_replacer.Victim(&value);
  EXPECT_EQ(2, value);
  EXPECT_EQ(1, clock_replacer.Size());

  // Remaining: [4], ref[4]=0
  clock_replacer.Victim(&value);
  EXPECT_EQ(4, value);
  EXPECT_EQ(0, clock_replacer.Size());
}

TEST(CLOCKReplacerTest, PinRemovesFromClock) {
  CLOCKReplacer clock_replacer(5);

  clock_replacer.Unpin(1);
  clock_replacer.Unpin(2);
  clock_replacer.Unpin(3);
  EXPECT_EQ(3, clock_replacer.Size());

  // Pin page 2: it should be removed from the clock and cannot be victimized.
  clock_replacer.Pin(2);
  EXPECT_EQ(2, clock_replacer.Size());

  int value;
  // Victim should be 1 or 3, but not 2.
  clock_replacer.Victim(&value);
  EXPECT_NE(2, value);

  // Pin a page not in the clock: should have no effect.
  clock_replacer.Pin(100);
  EXPECT_EQ(1, clock_replacer.Size());
}

TEST(CLOCKReplacerTest, EmptyReplacer) {
  CLOCKReplacer clock_replacer(5);

  // Victim on empty replacer should return false.
  int value;
  EXPECT_FALSE(clock_replacer.Victim(&value));
  EXPECT_EQ(0, clock_replacer.Size());

  // Pin/Unpin on empty replacer should not crash.
  clock_replacer.Pin(1);
  EXPECT_EQ(0, clock_replacer.Size());

  clock_replacer.Unpin(1);
  EXPECT_EQ(1, clock_replacer.Size());
}
