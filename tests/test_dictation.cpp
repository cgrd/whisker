// SPDX-License-Identifier: MIT
#include "check.h"
#include "dictation.h"

static void testEveryCommand() {
    CHECK_EQ(processDictation("a period b"), "a. b");
    CHECK_EQ(processDictation("a full stop b"), "a. b");
    CHECK_EQ(processDictation("a comma b"), "a, b");
    CHECK_EQ(processDictation("a question mark b"), "a? b");
    CHECK_EQ(processDictation("a exclamation mark b"), "a! b");
    CHECK_EQ(processDictation("a exclamation point b"), "a! b");
    CHECK_EQ(processDictation("a colon b"), "a: b");
    CHECK_EQ(processDictation("a semicolon b"), "a; b");
    CHECK_EQ(processDictation("a new line b"), "a\nb");
    CHECK_EQ(processDictation("a newline b"), "a\nb");
    CHECK_EQ(processDictation("a new paragraph b"), "a\n\nb");
    CHECK_EQ(processDictation("a open quote b close quote c"), "a \"b\" c");
    CHECK_EQ(processDictation("a open paren b close paren c"), "a (b) c");
    CHECK_EQ(processDictation("well hyphen known"), "well-known");
    CHECK_EQ(processDictation("a dash b"), "a - b");
    CHECK_EQ(processDictation("a ellipsis b"), "a... b");
}

static void testCombinations() {
    CHECK_EQ(processDictation("hello comma world period"), "hello, world.");
    CHECK_EQ(processDictation("he said open quote hi close quote period"), "he said \"hi\".");
    CHECK_EQ(processDictation("end period new line next"), "end.\nnext");
    CHECK_EQ(processDictation("open paren aside close paren comma then"), "(aside), then");
    CHECK_EQ(processDictation("state hyphen of hyphen the hyphen art"), "state-of-the-art");
    CHECK_EQ(processDictation("wait dash what question mark"), "wait - what?");
}

static void testEdges() {
    CHECK_EQ(processDictation(""), "");
    CHECK_EQ(processDictation("  plain   words  "), "plain words");
    CHECK_EQ(processDictation("periodic table"), "periodic table");
    CHECK_EQ(processDictation("period"), ".");
    CHECK_EQ(processDictation("dash"), "-");
    CHECK_EQ(processDictation("new line hello"), "\nhello");
}

static void testCaseInsensitive() {
    CHECK_EQ(processDictation("Hello Period"), "Hello.");
    CHECK_EQ(processDictation("yes FULL STOP"), "yes.");
    CHECK_EQ(processDictation("a New Line b"), "a\nb");
}

static void testAutoCapitalize() {
    CHECK_EQ(autoCapitalize(""), "");
    CHECK_EQ(autoCapitalize("hello. world? yes! ok"), "Hello. World? Yes! Ok");
    CHECK_EQ(autoCapitalize("first\nsecond"), "First\nSecond");
    CHECK_EQ(autoCapitalize("3 apples"), "3 Apples");
    CHECK_EQ(autoCapitalize("Already Fine."), "Already Fine.");
}

int main() {
    testEveryCommand();
    testCombinations();
    testEdges();
    testCaseInsensitive();
    testAutoCapitalize();
    return checkReport();
}
