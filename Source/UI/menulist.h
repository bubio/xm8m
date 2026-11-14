//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ menu list ]
//

#ifdef SDL

#ifndef MENULIST_H
#define MENULIST_H

//
// menu list
//
class MenuList
{
	friend class UiTestAccess;
public:
	MenuList(App *a);
										// constructor
	virtual ~MenuList();
										// destructor
	bool Init();
										// initialize
	void Deinit();
										// deinitialize

	// control
	void EnterMenu();
										// enter menu mode
	void ProcessMenu(bool joystick);
										// process menu
	void Clear();
										// clear all element
	bool SetTitle(const char *title, int id, bool file = false);
										// clear and set title
	void AddButton(const char *name, int id);
										// add push button
	void AddRadioButton(const char *name, int id, int group);
										// add radio button
	void AddCheckButton(const char *name, int id);
										// add check button
	void AddSlider(const char *name, int id, int minimum, int maximum, int unit);
										// add slider
	void Sort();
										// sort items
	const char* GetText(int id);
										// get item name
	void SetText(int id, const char *name);
										// set item name
	bool GetRadio(int id);
										// get radio state
	void SetRadio(int id, int group);
										// set radio state
	bool GetCheck(int id);
										// get check state
	void SetCheck(int id, bool check);
										// set check state
	int GetSlider(int id);
										// get slider state
	void SetSlider(int id, int current);
										// set slider state
	Uint64 GetUser(int id);
										// get user info
	void SetUser(int id, Uint64 user);
										// set user info
	int GetID();
										// get menu id
	int GetFocusID();
										// get focused item id
	int GetTop() const;
										// get first visible item index
	const char* GetName(int id);
										// get menu name
	void SetFocus(int id);
										// set focus id
	void Draw();
										// draw menu list

	// event
	void OnKeyDown(SDL_Event *e);
										// key down
	void OnMouseMotion(SDL_Event *e);
										// mouse motion
	void OnMouseButtonDown(SDL_Event *e);
										// mouse button down
	void OnMouseButtonUp(SDL_Event *e);
										// mouse button up
	void OnMouseWheel(SDL_Event *e);
										// mouse wheel
	void OnJoystick();
										// joystick
	void OnFingerDown(SDL_Event *e);
										// finger down
	void OnFingerUp(SDL_Event *e);
										// finger up
	void OnFingerMotion(SDL_Event *e);
										// finger motion

private:
	void AddCommon(MenuItem *item);
										// add common routine
	static int SortCallback(const void *ptr1, const void *ptr2);
										// sort callback routine
	MenuItem* GetItem(int index);
										// get menu item from index
	MenuItem* FindItem(int id);
										// get menu item from id
	bool FingerToItem(float tx, float ty, int *x, int *y);
										// finger to item
	bool PosToItem(int *x, int *y, bool finger = false);
										// screen position to item
	bool IsUp(int y);
										// check screen position to up scroll
	bool IsDown(int y);
										// check screen position to down scroll
	bool IsLow(int x, int y);
										// check screen position to spread area
	App *app;
										// application
	Setting *setting;
										// setting
	Video *video;
										// video
	Font *font;
										// font
	Input *input;
										// input
	Menu *menu;
										// menu
	Converter *converter;
										// converter
	Uint32 *frame_buf;
										// frame buffer
	MenuItem *menu_chain;
										// item chain
	char *menu_title;
										// title
	int menu_id;
										// id
	int menu_items;
										// the number of menu items
	int menu_top;
										// top of menu items
	int menu_focus;
										// menu index with focus
	Uint32 menu_tick;
										// mouse motion tick
	bool menu_file;
										// file menu flag
	int finger_x;
										// finger x on down
	int finger_y;
										// finger y on down
	Uint32 finger_tick;
										// finger tick on down
	Uint64 finger_counter;
										// monotonic finger counter on down
	bool finger_tap_valid;
										// no scroll or slider drag since finger down
	int finger_focus;
										// menu index with focus on finger down
	bool finger_slider;
										// finger slider enable
	bool mouse_down;
										// mouse down flag
	Uint32 mouse_tick;
										// mouse tick on enter
	Uint32 joy_prev;
										// joystick previous status
	Uint32 joy_tick;
										// joystick tick on any axis
	Uint32 joy_diff;
										// joystick diff for repeat
};

#endif // MENULIST_H

#endif // SDL
