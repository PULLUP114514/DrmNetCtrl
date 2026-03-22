#pragma once
#include <linux/input.h>
// 初始化
void keyinput_init();
// 获取输入的按钮。从上往下分别是KEY_1/KEY_2/KEY_3/KEY_4.电源是KEY_0
// 如果没有按钮被按住的话，返回-1
int keyinput_get_key();