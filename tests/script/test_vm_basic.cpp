// SPDX-License-Identifier: GPL-2.0-only
// End-to-end tests: expressions, control flow, strings, arrays.
#include <limits>

#include "vm_test_util.hpp"

using namespace mingol::test;

namespace {
// Wraps statements into `static int f()`.
std::string body(const std::string& stmts) { return "class Main { static int f() { " + stmts + " } }"; }
}  // namespace

TEST(VmBasic, IntegerArithmeticAndPrecedence) {
  CHECK_INT(body("return 1 + 2 * 3 - 4;"), 3);
  CHECK_INT(body("return (1 + 2) * (3 - 4);"), -3);
  CHECK_INT(body("return 7 / 2;"), 3);
  CHECK_INT(body("return -7 / 2;"), -3);
  CHECK_INT(body("return 7 % 3;"), 1);
  CHECK_INT(body("return -7 % 3;"), -1);
  CHECK_INT(body("return 1 << 10;"), 1024);
  CHECK_INT(body("return -16 >> 2;"), -4);
  CHECK_INT(body("return 6 & 3;"), 2);
  CHECK_INT(body("return 6 | 3;"), 7);
  CHECK_INT(body("return 6 ^ 3;"), 5);
  CHECK_INT(body("return ~5;"), -6);
  CHECK_INT(body("return - -3;"), 3);
  CHECK_INT(body("int a = 5; a += 3; a *= 2; a -= 1; a /= 3; a %= 4; return a;"), 1);
  CHECK_INT(body("int a = 3; a <<= 4; a >>= 1; a |= 1; a &= 0xFF; a ^= 2; return a;"), 27);
}

TEST(VmBasic, IntegerWrapAroundAndEdgeCases) {
  CHECK_INT(body("int m = 9223372036854775807; return m + 1 == -9223372036854775808 ? 1 : 0;"), 1);
  CHECK_INT(body("int m = -9223372036854775808; return m - 1 == 9223372036854775807 ? 1 : 0;"), 1);
  CHECK_INT(body("int m = -9223372036854775808; return m / -1 == m ? 1 : 0;"), 1);
  CHECK_INT(body("int m = -9223372036854775808; return m % -1;"), 0);
  CHECK_INT(body("int m = -9223372036854775808; return -m == m ? 1 : 0;"), 1);
  CHECK_INT(body("int a = 3037000500; return a * a < 0 ? 1 : 0;"), 1);
  CHECK_INT(body("return 1 << 64;"), 1);  // shift counts are masked to 6 bits
  CHECK_INT(body("return 1 << 65;"), 2);
  CHECK_INT(body("int s = 63; return (1 << s) < 0 ? 1 : 0;"), 1);
  CHECK_INT(body("return 0x7FFFFFFFFFFFFFFF == 9223372036854775807 ? 1 : 0;"), 1);
  CHECK_INT(body("return 0b1010 + 0xff;"), 265);
}

TEST(VmBasic, DivisionByZeroThrows) {
  CHECK_THROWS(body("int z = 0; return 1 / z;"), "mingol.DivideByZeroError");
  CHECK_THROWS(body("int z = 0; return 1 % z;"), "mingol.DivideByZeroError");
  CHECK_FLOAT("class Main { static float f() { float z = 0.0; return 1.0 / z; } }", std::numeric_limits<double>::infinity());
}

TEST(VmBasic, FloatArithmeticAndConversions) {
  CHECK_FLOAT("class Main { static float f() { return 1.5 + 2.25 * 2.0; } }", 6.0);
  CHECK_FLOAT("class Main { static float f() { return 7.5 % 2.0; } }", 1.5);
  CHECK_FLOAT("class Main { static float f() { int a = 3; return a / 2.0; } }", 1.5);
  CHECK_FLOAT("class Main { static float f() { int a = 3; float b = a; return b; } }", 3.0);
  CHECK_INT(body("return 3.99 as int;"), 3);
  CHECK_INT(body("return -3.99 as int;"), -3);
  CHECK_INT(body("float n = 0.0 / 0.0; return n as int;"), 0);
  CHECK_INT(body("float big = 1.0e30; return big as int == 9223372036854775807 ? 1 : 0;"), 1);
  CHECK_INT(body("float big = -1.0e30; return big as int == -9223372036854775808 ? 1 : 0;"), 1);
  CHECK_BOOL("class Main { static bool f() { float n = 0.0 / 0.0; return n == n; } }", false);
  CHECK_BOOL("class Main { static bool f() { float n = 0.0 / 0.0; return n != n; } }", true);
  CHECK_BOOL("class Main { static bool f() { return 1 == 1.0; } }", true);
  CHECK_BOOL("class Main { static bool f() { return 2 < 2.5; } }", true);
  CHECK_FLOAT("class Main { static float f() { float x = 1.0; x += 1; x *= 3; return x; } }", 6.0);
  CHECK_FLOAT("class Main { static float f() { return -(2.5); } }", -2.5);
  CHECK_FLOAT("class Main { static float f() { return 1e3 + 0.5e1; } }", 1005.0);
}

TEST(VmBasic, BooleansAndShortCircuit) {
  CHECK_BOOL("class Main { static bool f() { return true && !false; } }", true);
  CHECK_BOOL("class Main { static bool f() { return false || false; } }", false);
  CHECK_BOOL("class Main { static bool f() { return true & false | true ^ false; } }", true);
  CHECK_INT("class Main { static int n; static bool hit() { n++; return true; } static int f() { bool a = false && hit(); bool b = true || hit(); return n; } }", 0);
  CHECK_INT("class Main { static int n; static bool hit() { n++; return true; } static int f() { bool a = true && hit(); bool b = false || hit(); return n; } }", 2);
  CHECK_INT("class Main { static int n; static bool hit() { n++; return true; } static int f() { bool a = false & hit(); return n; } }", 1);  // non-short-circuit &
  CHECK_INT(body("return (3 > 2) ? 10 : 20;"), 10);
  CHECK_INT(body("int a = 5; return a < 3 ? 1 : a < 6 ? 2 : 3;"), 2);
  CHECK_FLOAT("class Main { static float f() { bool c = true; return c ? 1 : 2.5; } }", 1.0);
}

TEST(VmBasic, ControlFlow) {
  CHECK_INT(body("int s = 0; for (int i = 1; i <= 10; i++) { s += i; } return s;"), 55);
  CHECK_INT(body("int s = 0; int i = 0; while (i < 5) { s += i; i++; } return s;"), 10);
  CHECK_INT(body("int s = 0; int i = 0; do { s += 2; i++; } while (i < 3); return s;"), 6);
  CHECK_INT(body("int s = 0; for (int i = 0; i < 10; i++) { if (i == 3) continue; if (i == 6) break; s += i; } return s;"), 12);
  CHECK_INT(body("int s = 0; for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) { if (j == 2) break; s++; } } return s;"), 6);
  CHECK_INT(body("int s = 0; for (;;) { s++; if (s == 7) break; } return s;"), 7);
  CHECK_INT(body("int x = 4; if (x > 5) { return 1; } else if (x > 3) { return 2; } else { return 3; }"), 2);
  CHECK_INT(body("int i = 0; do { i++; if (i < 3) continue; break; } while (true); return i;"), 3);
}

TEST(VmBasic, IncrementAndDecrementForms) {
  CHECK_INT(body("int i = 5; int a = i++; int b = ++i; int c = i--; int d = --i; return a * 1000 + b * 100 + c * 10 + d;"), 5 * 1000 + 7 * 100 + 7 * 10 + 5);
  CHECK_FLOAT("class Main { static float f() { float x = 1.5; x++; ++x; return x; } }", 3.5);
  CHECK_INT("class Main { static int[] a; static int f() { a = [1, 2, 3]; a[1]++; int v = a[2]++; return a[1] * 100 + a[2] * 10 + v; } }", 3 * 100 + 4 * 10 + 3);
  CHECK_INT("class Box { int v; } class Main { static int f() { Box b = new Box(); b.v++; b.v += 5; int r = b.v--; return b.v * 10 + r; } }", 5 * 10 + 6);
  CHECK_INT("class Main { static int s; static int f() { s = 4; s++; ++s; s *= 2; return s; } }", 12);
}

TEST(VmBasic, AssignmentIsAnExpression) {
  CHECK_INT(body("int a; int b; a = b = 4; return a + b;"), 8);
  CHECK_INT(body("int a = 1; int b = (a = 5) + 1; return a * 10 + b;"), 56);
  CHECK_INT("class Main { static int[] v; static int f() { v = new int[3]; int x = v[1] = 9; return x + v[1]; } }", 18);
  CHECK_INT("class P { int x; } class Main { static int f() { P p = new P(); int y = p.x = 6; return y * p.x; } }", 36);
  CHECK_INT(body("int a = 2; a += a += 3; return a;"), 7);  // left operand is read first
}

TEST(VmBasic, StringsConcatConvertCompare) {
  CHECK_STR("class Main { static string f() { return \"a\" + \"b\" + 1 + 2.5 + true; } }", "ab12.5true");
  CHECK_STR("class Main { static string f() { return 1 + \"x\"; } }", "1x");
  CHECK_STR("class Main { static string f() { return \"v=\" + 2.0; } }", "v=2.0");
  CHECK_STR("class Main { static string f() { return \"v=\" + (0.1 + 0.2); } }", "v=0.30000000000000004");
  CHECK_STR("class Main { static string f() { return \"v=\" + (-1.5e-7); } }", "v=-1.5e-7");
  CHECK_STR("class Main { static string f() { return \"v=\" + 9223372036854775807; } }", "v=9223372036854775807");
  CHECK_STR("class Main { static string f() { return \"v=\" + -9223372036854775808; } }", "v=-9223372036854775808");
  CHECK_STR("class Main { static string f() { float n = 0.0 / 0.0; return \"\" + n; } }", "nan");
  CHECK_STR("class Main { static string f() { float n = 1.0 / 0.0; return \"\" + n + (-n); } }", "inf-inf");
  CHECK_STR("class Main { static string f() { string s = \"a\"; s += \"b\"; s += 3; return s; } }", "ab3");
  CHECK_STR("class Main { static string f() { return 42 as string; } }", "42");
  CHECK_STR("class Main { static string f() { return 1.5 as string; } }", "1.5");
  CHECK_STR("class Main { static string f() { return true as string; } }", "true");
  CHECK_STR("class Main { static string f() { return \"line\\n\\t\\\"q\\\" \\u{AC00}\"; } }", "line\n\t\"q\" \xEA\xB0\x80");
  CHECK_BOOL("class Main { static bool f() { return \"abc\" == \"abc\"; } }", true);
  CHECK_BOOL("class Main { static bool f() { string a = \"ab\"; string b = \"a\" + \"b\"; return a == b; } }", true);
  CHECK_BOOL("class Main { static bool f() { return \"abc\" != \"abd\"; } }", true);
  CHECK_BOOL("class Main { static bool f() { return \"abc\" < \"abd\" && \"b\" > \"abc\" && \"a\" <= \"a\" && \"a\" >= \"a\"; } }", true);
  CHECK_BOOL("class Main { static bool f() { return \"\" < \"a\"; } }", true);
}

TEST(VmBasic, StringLengthAndIndexAreBytes) {
  CHECK_INT(body("string s = \"hello\"; return s.length;"), 5);
  CHECK_INT(body("string s = \"\"; return s.length;"), 0);
  CHECK_INT(body("string s = \"\\u{AC00}\"; return s.length;"), 3);  // UTF-8 bytes, documented
  CHECK_INT(body("string s = \"AZ\"; return s[0] * 1000 + s[1];"), 65 * 1000 + 90);
  CHECK_THROWS(body("string s = \"ab\"; return s[2];"), "mingol.IndexOutOfRangeError");
  CHECK_THROWS(body("string s = \"ab\"; int i = -1; return s[i];"), "mingol.IndexOutOfRangeError");
}

TEST(VmBasic, Arrays) {
  CHECK_INT(body("int[] a = new int[4]; a[2] = 7; return a.length * 10 + a[2] + a[0];"), 47);
  CHECK_INT(body("int[] a = [1, 2, 3, 4]; int s = 0; for (int v : a) { s += v; } return s;"), 10);
  CHECK_INT(body("int[][] g = new int[3][]; g[1] = [5, 6]; return g[1][1] + (g[0] == null ? 100 : 0);"), 106);
  CHECK_FLOAT("class Main { static float f() { float[] a = new float[2]; a[0] = 1; a[1] = 2.5; return a[0] + a[1]; } }", 3.5);
  CHECK_FLOAT("class Main { static float f() { float[] a = [1, 2, 3]; return a[0] + a[2]; } }", 4.0);  // ints widen in literals
  CHECK_STR("class Main { static string f() { string[] a = new string[2]; a[1] = \"x\"; return a[0] + a[1] + a.length; } }", "x2");
  CHECK_BOOL("class Main { static bool f() { bool[] a = new bool[1]; return !a[0]; } }", true);
  CHECK_INT(body("int[] a = []; return a.length;"), 0);
  CHECK_INT(body("int[] a = new int[0]; int s = 0; for (int v : a) { s++; } return s;"), 0);
  CHECK_INT(body("int[] a = [3, 1, 2]; int[] b = a; b[0] = 9; return a[0];"), 9);  // arrays are references
  CHECK_INT("class Main { static int sum(int[] xs) { int s = 0; for (var x : xs) { s += x; } return s; } static int f() { return sum([1, 2, 3]) + sum([]); } }", 6);
}

TEST(VmBasic, ArrayErrors) {
  CHECK_THROWS(body("int[] a = new int[2]; return a[2];"), "mingol.IndexOutOfRangeError");
  CHECK_THROWS(body("int[] a = new int[2]; int i = -1; return a[i];"), "mingol.IndexOutOfRangeError");
  CHECK_THROWS(body("int[] a = new int[2]; a[5] = 1; return 0;"), "mingol.IndexOutOfRangeError");
  CHECK_THROWS(body("int n = -1; int[] a = new int[n]; return 0;"), "mingol.ArgumentError");
  CHECK_THROWS(body("int n = 9223372036854775807; int[] a = new int[n]; return 0;"), "mingol.OutOfMemoryError");
  CHECK_THROWS(body("int[] a = null; return a[0];"), "mingol.NullReferenceError");
  CHECK_THROWS(body("int[] a = null; return a.length;"), "mingol.NullReferenceError");
  CHECK_THROWS(body("int[] a = null; for (int v : a) { } return 0;"), "mingol.NullReferenceError");
}

TEST(VmBasic, EnumsAndSwitch) {
  const std::string e = "enum Color { Red, Green = 5, Blue } ";
  CHECK_INT(e + body("return (Color.Green as int) + (Color.Blue as int);"), 5 + 6);
  CHECK_INT(e + body("Color c = Color.Blue; return c == Color.Blue ? 1 : 0;"), 1);
  CHECK_INT(e + body("return 5 as Color == Color.Green ? 1 : 0;"), 1);
  CHECK_THROWS(e + body("int n = 3; Color c = n as Color; return 0;"), "mingol.CastError");
  CHECK_STR(e + "class Main { static string f() { Color c = Color.Green; return \"c=\" + c; } }", "c=Green");
  CHECK_INT(e + body("Color c = Color.Green; switch (c) { case Color.Red: return 1; case Color.Green: return 2; default: return 3; }"), 2);
  CHECK_INT(body("int x = 3; switch (x) { case 1: return 10; case 2, 3: return 20; default: return 30; }"), 20);
  CHECK_INT(body("int x = 9; switch (x) { case 1: return 10; default: return 30; }"), 30);
  CHECK_INT(body("int x = 9; int r = 0; switch (x) { case 1: r = 10; break; } return r;"), 0);
  CHECK_INT(body("string s = \"b\"; switch (s) { case \"a\": return 1; case \"b\": return 2; default: return 3; }"), 2);
  CHECK_INT(body("int x = -4; switch (x) { case -4: return 7; default: return 0; }"), 7);
  CHECK_INT(body("int s = 0; for (int i = 0; i < 4; i++) { switch (i) { case 1: continue; case 2: break; default: s += 10; } s += 1; } return s;"), 23);
}

TEST(VmBasic, NullReferencesAndComparisons) {
  CHECK_THROWS("class P { int x; } class Main { static int f() { P p = null; return p.x; } }", "mingol.NullReferenceError");
  CHECK_THROWS("class P { int x; } class Main { static int f() { P p = null; p.x = 1; return 0; } }", "mingol.NullReferenceError");
  CHECK_THROWS("class P { int g() { return 1; } } class Main { static int f() { P p = null; return p.g(); } }", "mingol.NullReferenceError");
  CHECK_BOOL("class P { } class Main { static bool f() { P a = new P(); P b = a; return a == b && a != new P() && a != null; } }", true);
  CHECK_BOOL("class P { } class Main { static bool f() { P a = null; return a == null; } }", true);
  CHECK_THROWS("class E extends Exception { } class Main { static int f() { Exception e = null; throw e; } }", "mingol.NullReferenceError");
}
