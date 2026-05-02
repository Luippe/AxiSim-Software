#pragma once

class BaseGUI {
public:
	void changeCursorOnHover();
	void textToLeft(const char* text);
	void setTableColumn(const int column);
	void tableNextRow();
	void tableNextColumn();
	void textAtNewRow(const char* text, const int column, const int nextColumn);
};