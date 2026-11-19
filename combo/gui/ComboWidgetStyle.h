#pragma once

#include <imgui.h>

namespace ComboRando {

ImVec4 ComboMenu_ThemeColor();

void ComboMenu_PushCheckbox(const ImVec4& color);
void ComboMenu_PopCheckbox();

void ComboMenu_PushCombobox(const ImVec4& color);
void ComboMenu_PopCombobox();

void ComboMenu_PushSlider(const ImVec4& color);
void ComboMenu_PopSlider();

void ComboMenu_PushButton(const ImVec4& color);
void ComboMenu_PopButton();

void ComboMenu_PushInput(const ImVec4& color);
void ComboMenu_PopInput();

bool ComboMenu_StyledHeaderEntry(const char* label, bool selected);
bool ComboMenu_StyledSidebarEntry(const char* label, bool selected, float width);

} // namespace ComboRando
