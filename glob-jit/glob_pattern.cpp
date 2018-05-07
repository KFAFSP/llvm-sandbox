#include "glob_pattern.h"

bool matchGenericHelper(const char *pattern, const char *testString, int i,
                        int j) {
  if (pattern[i] == '\0')
    return testString[j] == '\0';

  switch (pattern[i]) {
  default:
    return testString[j] != '\0' && pattern[i] == testString[j] &&
           matchGenericHelper(pattern, testString, i + 1, j + 1);
  case '?':
    return testString[j] != '\0' &&
           matchGenericHelper(pattern, testString, i + 1, j + 1);
  case '*':
    for (int k = j; testString[k] != '\0'; ++k) {
      if (matchGenericHelper(pattern, testString, i + 1, k))
        return true;
    }
    return false;
  }
}

bool matchGeneric(const char *pattern, const char *testString) {
  return matchGenericHelper(pattern, testString, 0, 0);
}

bool matchFixedHelper(const char *testString, int i, int j) {
  char pattern[7] = "a*b*c?";
  if (pattern[i] == '\0')
    return testString[j] == '\0';

  switch (pattern[i]) {
  default:
    return testString[j] != '\0' && pattern[i] == testString[j] &&
           matchFixedHelper(testString, i + 1, j + 1);
  case '?':
    return testString[j] != '\0' && matchFixedHelper(testString, i + 1, j + 1);
  case '*':
    for (int k = j; testString[k] != '\0'; ++k) {
      if (matchFixedHelper(testString, i + 1, k))
        return true;
    }
    return false;
  }
}

bool matchFixed(const char *testString) {
  return matchFixedHelper(testString, 0, 0);
}
