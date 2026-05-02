#pragma once

class BaseGUI {
public:
	void changeCursorOnHover();
	void textToLeft(const char* text);
	void setTableColumn(const int column);
	void tableNextRow();
	void tableNextColumn();

	void setNextWindowSize(int height, int width);

	// helper function to move to next row, set text, and move to next column
	void textAtNewRow(const char* text, const int column, const int nextColumn);

	// create a simple combo box with label and items, current item is updated by reference
	void createSimpleCombo(const char* label, const char* items[], int& currentItem, int itemCount);
};