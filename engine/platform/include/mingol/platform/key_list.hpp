// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// MKEY(Identifier, GLFW_KEY_ suffix, "script-visible name")
// Single source of truth for the Key enum, key names and the GLFW translation table.
#define MINGOL_KEY_LIST(MKEY)                                                                         \
  MKEY(Space, SPACE, "Space") MKEY(Apostrophe, APOSTROPHE, "'") MKEY(Comma, COMMA, ",")                      \
  MKEY(Minus, MINUS, "-") MKEY(Period, PERIOD, ".") MKEY(Slash, SLASH, "/")                                  \
  MKEY(Num0, 0, "0") MKEY(Num1, 1, "1") MKEY(Num2, 2, "2") MKEY(Num3, 3, "3") MKEY(Num4, 4, "4")                   \
  MKEY(Num5, 5, "5") MKEY(Num6, 6, "6") MKEY(Num7, 7, "7") MKEY(Num8, 8, "8") MKEY(Num9, 9, "9")                   \
  MKEY(Semicolon, SEMICOLON, ";") MKEY(Equal, EQUAL, "=")                                                 \
  MKEY(A, A, "A") MKEY(B, B, "B") MKEY(C, C, "C") MKEY(D, D, "D") MKEY(E, E, "E") MKEY(F, F, "F") MKEY(G, G, "G")        \
  MKEY(H, H, "H") MKEY(I, I, "I") MKEY(J, J, "J") MKEY(K, K, "K") MKEY(L, L, "L") MKEY(M, M, "M") MKEY(N, N, "N")        \
  MKEY(O, O, "O") MKEY(P, P, "P") MKEY(Q, Q, "Q") MKEY(R, R, "R") MKEY(S, S, "S") MKEY(T, T, "T") MKEY(U, U, "U")        \
  MKEY(V, V, "V") MKEY(W, W, "W") MKEY(X, X, "X") MKEY(Y, Y, "Y") MKEY(Z, Z, "Z")                                  \
  MKEY(LeftBracket, LEFT_BRACKET, "[") MKEY(Backslash, BACKSLASH, "\\") MKEY(RightBracket, RIGHT_BRACKET, "]") \
  MKEY(GraveAccent, GRAVE_ACCENT, "`")                                                                 \
  MKEY(Escape, ESCAPE, "Escape") MKEY(Enter, ENTER, "Enter") MKEY(Tab, TAB, "Tab")                           \
  MKEY(Backspace, BACKSPACE, "Backspace") MKEY(Insert, INSERT, "Insert") MKEY(Delete, DELETE, "Delete")      \
  MKEY(Right, RIGHT, "Right") MKEY(Left, LEFT, "Left") MKEY(Down, DOWN, "Down") MKEY(Up, UP, "Up")              \
  MKEY(PageUp, PAGE_UP, "PageUp") MKEY(PageDown, PAGE_DOWN, "PageDown") MKEY(Home, HOME, "Home")             \
  MKEY(End, END, "End") MKEY(CapsLock, CAPS_LOCK, "CapsLock") MKEY(ScrollLock, SCROLL_LOCK, "ScrollLock")    \
  MKEY(NumLock, NUM_LOCK, "NumLock") MKEY(PrintScreen, PRINT_SCREEN, "PrintScreen") MKEY(Pause, PAUSE, "Pause") \
  MKEY(F1, F1, "F1") MKEY(F2, F2, "F2") MKEY(F3, F3, "F3") MKEY(F4, F4, "F4") MKEY(F5, F5, "F5") MKEY(F6, F6, "F6")   \
  MKEY(F7, F7, "F7") MKEY(F8, F8, "F8") MKEY(F9, F9, "F9") MKEY(F10, F10, "F10") MKEY(F11, F11, "F11")             \
  MKEY(F12, F12, "F12")                                                                                \
  MKEY(Kp0, KP_0, "Kp0") MKEY(Kp1, KP_1, "Kp1") MKEY(Kp2, KP_2, "Kp2") MKEY(Kp3, KP_3, "Kp3")                   \
  MKEY(Kp4, KP_4, "Kp4") MKEY(Kp5, KP_5, "Kp5") MKEY(Kp6, KP_6, "Kp6") MKEY(Kp7, KP_7, "Kp7")                   \
  MKEY(Kp8, KP_8, "Kp8") MKEY(Kp9, KP_9, "Kp9") MKEY(KpDecimal, KP_DECIMAL, "KpDecimal")                     \
  MKEY(KpDivide, KP_DIVIDE, "KpDivide") MKEY(KpMultiply, KP_MULTIPLY, "KpMultiply")                       \
  MKEY(KpSubtract, KP_SUBTRACT, "KpSubtract") MKEY(KpAdd, KP_ADD, "KpAdd") MKEY(KpEnter, KP_ENTER, "KpEnter") \
  MKEY(KpEqual, KP_EQUAL, "KpEqual")                                                                   \
  MKEY(LeftShift, LEFT_SHIFT, "LeftShift") MKEY(LeftControl, LEFT_CONTROL, "LeftControl")                 \
  MKEY(LeftAlt, LEFT_ALT, "LeftAlt") MKEY(LeftSuper, LEFT_SUPER, "LeftSuper")                             \
  MKEY(RightShift, RIGHT_SHIFT, "RightShift") MKEY(RightControl, RIGHT_CONTROL, "RightControl")           \
  MKEY(RightAlt, RIGHT_ALT, "RightAlt") MKEY(RightSuper, RIGHT_SUPER, "RightSuper") MKEY(Menu, MENU, "Menu")
