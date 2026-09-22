//
// eXcellent Multi-Platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ menu list ]
//

#ifdef SDL

#include "os.h"
#include "common.h"
#include "vm.h"
#include "app.h"
#include "setting.h"
#include "video.h"
#include "font.h"
#include "input.h"
#include "converter.h"
#include "menu.h"
#include "menuitem.h"
#include "menuid.h"
#include "menulist.h"

//
// defines
//
#define MENU_MOTION_TICK_MASK	0xffffff80
										// OnMouseMotion() tick mask
#define FINGER_TIME_THRES		250
										// OnFingerDownn() vs OnFingerUp() threshold (ms)
#define FINGER_SLIDER_THRES		250
										// OnFingerMotion() slider threshold (ms)
#define FINGER_POS_THRES		24
										// OnFingerMotion() slider threshold (x position)
#define MENU_ENTER_THRES		250
										// OnMouseUp() threshold (ms)
#define MENU_JOY_FIRST			350
										// joystick first wait (ms)
#define MENU_JOY_REPEAT			120
										// joystick repeat wait (ms)

//
// MenuList()
// constructor
//
MenuList::MenuList(App *a)
{
	// save parameter
	app = a;

	// object
	setting = NULL;
	video = NULL;
	font = NULL;
	input = NULL;
	menu = NULL;
	converter = NULL;
	frame_buf = NULL;

	// property
	menu_chain = NULL;
	menu_title = NULL;
	menu_id = -1;
	menu_items = 0;
	menu_top = 0;
	menu_focus = 0;
	menu_tick = 0;
	menu_file = false;

	// finger
	finger_x = -1;
	finger_y = -1;
	finger_tick = 0;
	finger_counter = 0;
	finger_tap_valid = false;
	finger_focus = -1;
	finger_slider = false;

	// mouse
	mouse_down = false;
	mouse_tick = 0;

	// joystick
	joy_prev = 0x30;
	joy_tick = 0;
	joy_diff = 0;
}

//
// ~MenuList()
// destructor
//
MenuList::~MenuList()
{
	Deinit();
}

//
// Init()
// initialize
//
bool MenuList::Init()
{
	// get object and frame buffer
	setting = app->GetSetting();
	video = app->GetVideo();
	font = app->GetFont();
	input = app->GetInput();
	menu = app->GetMenu();
	converter = app->GetConverter();
	frame_buf = video->GetMenuFrame();

	return true;
}

//
// Deinit()
// deinitialize
//
void MenuList::Deinit()
{
	// clear all elements
	Clear();
}

//
// EnterMenu()
// enter menu mode
//
void MenuList::EnterMenu()
{
	// finger
	finger_x = -1;
	finger_y = -1;
	finger_tick = 0;
	finger_counter = 0;
	finger_tap_valid = false;

	// mouse
	mouse_down = false;
	mouse_tick = SDL_GetTicks();

	// joystick
	joy_prev = 0x30;
	joy_tick = SDL_GetTicks();
	joy_diff = 0;
}

//
// ProcessMenu()
// process menu
//
void MenuList::ProcessMenu(bool joystick)
{
	int x;
	int y;
	Uint32 button;
	SDL_Event evt;

	// OnJoystick
	if (joystick == true) {
		OnJoystick();
	}
	else {
		joy_prev = 0x30;
	}

	// get mouse state
	button = SDL_GetMouseState(&x, &y);
	if (((button & SDL_BUTTON_LMASK) == 0) || (mouse_down == false)) {
		return;
	}

	// make event
	evt.motion.which = 0;
	evt.motion.x = x;
	evt.motion.y = y;

	// OnMouseMotion
	OnMouseMotion(&evt);
}

//
// Clear()
// clear all elements
//
void MenuList::Clear()
{
	MenuItem *last;
	MenuItem *prev;

	// chain
	while (menu_items > 1) {
		prev = GetItem(menu_items - 2);
		last = prev->GetNext();
		prev->Del(last);
		delete last;
		menu_items--;
	}
	if (menu_items > 0) {
		delete menu_chain;
		menu_chain = NULL;
		menu_items--;
	}

	// title
	if (menu_title != NULL) {
		SDL_free(menu_title);
		menu_title = NULL;
	}

	// id
	menu_id = -1;
}

//
// SetTitle()
// clear and set title
//
bool MenuList::SetTitle(const char *title, int id, bool file)
{
	char *sjis;
	char *ptr;
	Uint16 high;

	SDL_assert(title != NULL);
	SDL_assert(id > 0);

	// all clear
	Clear();

	// convert to shift-jis
	sjis = (char*)SDL_malloc(strlen(title) * 3 + 1);
	if (sjis == NULL) {
		return false;
	}
	converter->UtfToSjis(title, sjis);


	// check length
	ptr = sjis;
	while (strlen(ptr) > ((MENUITEM_WIDTH / 8) - 1)) {
		high = (Uint16)*ptr;
		high &= 0x00ff;
		if ((high >= 0x80) && (high <= 0xa0)) {
			ptr += 2;
		}
		else {
			if (high >= 0xe0) {
				ptr += 2;
			}
			else {
				ptr++;
			}
		}
	}

	// copy and free
	menu_title = (char*)SDL_malloc(strlen(ptr) + 1);
	if (menu_title == NULL) {
		SDL_free(sjis);
		return false;
	}
	strcpy(menu_title, ptr);
	SDL_free(sjis);

	// id
	menu_id = id;

	// file
	menu_file = file;

	// top and focus
	menu_top = 0;
	menu_focus = 0;

	return true;
}

//
// AddButton()
// add push button
//
void MenuList::AddButton(const char *name, int id)
{
	MenuItem *item;

	SDL_assert(name != NULL);
	SDL_assert(id > 0);
	SDL_assert(menu_id > 0);

	// create item;
	item = new MenuItem(app);
	item->Init(menu_items, MenuItem::ButtonItem, name, id, -1);

	// common
	AddCommon(item);
}

//
// AddRadioButton()
// add radio button
//
void MenuList::AddRadioButton(const char *name, int id, int group)
{
	MenuItem *item;

	SDL_assert(name != NULL);
	SDL_assert(id > 0);
	SDL_assert(menu_id > 0);

	// create item;
	item = new MenuItem(app);
	item->Init(menu_items, MenuItem::RadioItem, name, id, group);

	// common
	AddCommon(item);
}

//
// AddCheckButton()
// add check button
//
void MenuList::AddCheckButton(const char *name, int id)
{
	MenuItem *item;

	SDL_assert(name != NULL);
	SDL_assert(id > 0);
	SDL_assert(menu_id > 0);

	// create item;
	item = new MenuItem(app);
	item->Init(menu_items, MenuItem::CheckItem, name, id, -1);

	// common
	AddCommon(item);
}

//
// AddSlider()
// add slider
//
void MenuList::AddSlider(const char *name, int id, int minimum, int maximum, int unit)
{
	MenuItem *item;

	SDL_assert(name != NULL);
	SDL_assert(id > 0);
	SDL_assert(menu_id > 0);

	// create item;
	item = new MenuItem(app);
	item->Init(menu_items, MenuItem::SliderItem, name, id, -1);
	item->SetMinMax(minimum, maximum, unit);

	// common
	AddCommon(item);
}

//
// AddCommon()
// add common routine
//
void MenuList::AddCommon(MenuItem *item)
{
	MenuItem *last;

	// link list
	if (menu_chain == NULL) {
		menu_chain = item;
	}
	else {
		last = menu_chain;
		while (last->GetNext() != NULL) {
			last = last->GetNext();
		}
		last->Add(item);
	}

	// items
	menu_items++;
}

//
// Sort()
// sort items
//
void MenuList::Sort()
{
	MenuItem **list;
	MenuItem *ptr;
	int loop;

	// no item ?
	if (menu_items == 0) {
		return;
	}

	// malloc pointer list
	list = (MenuItem**)SDL_malloc(sizeof(MenuItem*) * menu_items);
	if (list == NULL) {
		return;
	}

	// set pointer
	ptr = menu_chain;
	for (loop=0; loop<menu_items; loop++) {
		list[loop] = ptr;
		ptr = ptr->GetNext();
	}

	// quick sort
	qsort(list, menu_items, sizeof(MenuItem*), SortCallback);

	// set index and pointer
	for (loop=0; loop<menu_items; loop++) {
		// first item ?
		if (loop == 0) {
			// first item
			menu_chain = list[0];
			menu_chain->SetNext(NULL);
		}

		// set index
		list[loop]->SetIndex(loop);

		// last item ?
		if (loop == (menu_items - 1)) {
			// last item
			continue;
		}

		// first to (last - 1)
		list[loop]->SetNext(list[loop + 1]);
	}

	// free pointer list
	SDL_free(list);
	list = NULL;
}

//
// SortCallback()
// qsort call back
//
int MenuList::SortCallback(const void *ptr1, const void *ptr2)
{
	MenuItem *item1;
	MenuItem *item2;
	const char *name1;
	const char *name2;

	// cast
	item1 = *(MenuItem**)ptr1;
	item2 = *(MenuItem**)ptr2;

	// name
	name1 = item1->GetName();
	name2 = item2->GetName();

	// period ?
	if ((name1[0] == '.') && (name2[0] != '.')) {
		return -1;
	}
	if ((name1[0] != '.') && (name2[0] == '.')) {
		return 1;
	}

#ifdef _WIN32
	// drive:root ?
	if (strlen(name1) == 3) {
		if ((name1[1] == ':') && (name1[2] == '\\')) {
			if (strlen(name2) != 3) {
				return 1;
			}
			if ((name2[1] == ':') && (name2[2] == '\\')) {
				return strcmp(name1, name2);
			}
			return 1;
		}
	}

	if (strlen(name2) == 3) {
		if ((name2[1] == ':') && (name2[2] == '\\')) {
			if (strlen(name1) != 3) {
				return -1;
			}
			if ((name1[1] == ':') && (name1[2] == '\\')) {
				return strcmp(name1, name2);
			}
			return -1;
		}
	}
#endif // _WIN32

	// strcmp
	return strcmp(name1, name2);
}

//
// GetText()
// get item name
//
const char* MenuList::GetText(int id)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	return item->GetName();
}

//
// SetText()
// set item name
//
void MenuList::SetText(int id, const char *name)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	item->SetName(name);
}

//
// GetRadio()
// get radio state
//
bool MenuList::GetRadio(int id)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	return item->GetRadio();
}

//
// SetRadio()
// set radio state
//
void MenuList::SetRadio(int id, int group)
{
	int loop;
	MenuItem *item;
	
	item = menu_chain;
	for (loop=0; loop<menu_items; loop++) {
		// get group
		if (item->GetGroup() == group) {
			// id
			if (item->GetID() == id) {
				item->SetRadio(true);
			}
			else {
				item->SetRadio(false);
			}
		}

		// next
		item = item->GetNext();
	}
}

//
// GetCheck()
// get check state
//
bool MenuList::GetCheck(int id)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	return item->GetCheck();
}

//
// SetCheck()
// set check state
//
void MenuList::SetCheck(int id, bool check)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	item->SetCheck(check);
}

//
// GetSlider()
// get slider state
//
int MenuList::GetSlider(int id)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	return item->GetSlider();
}

//
// SetSlider()
// set slider state
//
void MenuList::SetSlider(int id, int current)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	item->SetSlider(current);
}

//
// GetUser()
// get user info
//
Uint64 MenuList::GetUser(int id)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	return item->GetUser();
}

//
// SetUser()
// set user info
//
void MenuList::SetUser(int id, Uint64 user)
{
	MenuItem *item;

	item = FindItem(id);
	SDL_assert(item != NULL);

	item->SetUser(user);
}

//
// GetID()
// get menu id
//
int MenuList::GetID()
{
	return menu_id;
}

//
// GetFocusID()
// get focused item id
//
int MenuList::GetFocusID()
{
	MenuItem *item = GetItem(menu_focus);
	return item != NULL ? item->GetID() : MENU_BACK;
}

//
// GetTop()
// get first visible item index
//
int MenuList::GetTop() const
{
	return menu_top;
}

//
// GetName()
// get menu name
//
const char* MenuList::GetName(int id)
{
	MenuItem *item;

	item = FindItem(id);
	if (item != NULL) {
		return item->GetName();
	}
	else {
		return NULL;
	}
}

//
// SetFocus()
// set focus to specified id
//
void MenuList::SetFocus(int id)
{
	int loop;
	int num;
	MenuItem *item;
	SDL_Event evt;

	// initialize
	menu_focus = 0;

	// get number from id
	num = 0;
	for (loop=0; loop<menu_items; loop++) {
		item = GetItem(loop);
		if (item->GetID() == id) {
			num = loop;
			break;
		}
	}

	// event
	evt.key.repeat = 0;
	evt.key.keysym.sym = SDLK_DOWN;
	evt.key.keysym.scancode = SDL_SCANCODE_DOWN;

	// loop
	for (loop=0; loop<menu_items; loop++) {
		if (menu_focus == num) {
			break;
		}

		// key down
		OnKeyDown(&evt);
	}
}

//
// GetItem()
// get menu item from index
//
MenuItem* MenuList::GetItem(int index)
{
	int loop;
	MenuItem *item;

	// initialize
	item = menu_chain;

	for (loop=0; loop<index; loop++) {
		item = item->GetNext();
		SDL_assert(item != NULL);
	}

	return item;
}

//
// FindItem()
// get menu item from id
//
MenuItem* MenuList::FindItem(int id)
{
	int loop;
	MenuItem *item;

	item = menu_chain;

	for (loop=0; loop<menu_items; loop++) {
		// id
		if (item->GetID() == id) {
			return item;
		}

		// next
		item = item->GetNext();
		SDL_assert(item != NULL);
	}

	return NULL;
}

//
// Draw()
// draw menu list
//
void MenuList::Draw()
{
	Uint32 alpha;
	Uint32 fore;
	Uint32 back;
	int lines;
	int loop;
	SDL_Rect rect;
	SDL_Rect title_rect;
	MenuItem *item;

	// check
	if ((menu_title == NULL) || (menu_chain == NULL)) {
		return;
	}

	// get color
	alpha = (Uint32)setting->GetMenuAlpha();
	alpha <<= 24;
	fore = MENUITEM_FORE | alpha;
	back = MENUITEM_BACK | alpha;

	// get lines
	lines = menu_items + 1;
	if (MENUITEM_LINES < lines) {
		lines = MENUITEM_LINES;
	}

	// get rect
	rect.x = (SCREEN_WIDTH / 2) - (MENUITEM_WIDTH / 2);
	rect.y = (SCREEN_HEIGHT / 2) - ((MENUITEM_HEIGHT * MENUITEM_LINES) / 2);
	rect.w = MENUITEM_WIDTH;
	rect.h = MENUITEM_HEIGHT * lines;

	// all clear
	title_rect.x = rect.x;
	title_rect.y = rect.y;
	title_rect.w = rect.w;
	title_rect.h = MENUITEM_HEIGHT * MENUITEM_LINES;
	font->DrawFillRect(frame_buf, &title_rect, MENUITEM_BACK | 0x00000000);

	// title (outside)
	title_rect.x = rect.x;
	title_rect.y = rect.y;
	title_rect.w = rect.w;
	title_rect.h = MENUITEM_HEIGHT;
	font->DrawFillRect(frame_buf, &title_rect, fore);

	// title (inside)
	back = MENUITEM_TITLE | alpha;
	title_rect.x++;
	title_rect.y++;
	title_rect.w -= 2;
	title_rect.h -= 2;
	font->DrawFillRect(frame_buf, &title_rect, back);
	font->DrawSjisCenterOr(frame_buf, &title_rect, menu_title, fore);

	// all items
	for (loop=0; loop<menu_items; loop++) {
		item = GetItem(loop);
		item->Draw(&rect, menu_top, menu_items, menu_focus, menu_file);
	}
}

//
// OnKeyDown()
// key down event
//
void MenuList::OnKeyDown(SDL_Event *e)
{
	SDL_Scancode code;
	MenuItem *item;
	int count;
	int loop;

	// scancode
	code = e->key.keysym.scancode;

	// count
	count = 1;

	switch (code) {
	case SDL_SCANCODE_F11:
		app->LeaveMenu();
		break;

#ifdef __ANDROID__
	case SDL_SCANCODE_AC_BACK:
#endif // __ANDROID__
	case SDL_SCANCODE_ESCAPE:
		if (e->key.repeat == 0) {
			menu->Command(false, MENU_BACK);
		}
		break;

	case SDL_SCANCODE_HOME:
		if (count == 1) {
			count = menu_items;
		}
		// fall through

	case SDL_SCANCODE_PAGEUP:
		if (count == 1) {
			count = MENUITEM_LINES - 1;
		}
		// fall through

	case SDL_SCANCODE_UP:
	case SDL_SCANCODE_KP_8:
		for (loop=0; loop<count; loop++) {
			if (menu_focus > 0) {
				menu_focus--;
				item = GetItem(menu_focus);
				item->SetFocus();
			}
			if ((menu_focus - menu_top) < (MENUITEM_LINES - 6)) {
				if (menu_top > 0) {
					menu_top--;
				}
			}
		}
		break;

	case SDL_SCANCODE_END:
		if (count == 1) {
			count = menu_items;
		}
		// fall through

	case SDL_SCANCODE_PAGEDOWN:
		if (count == 1) {
			count = MENUITEM_LINES - 1;
		}
		// fall through

	case SDL_SCANCODE_DOWN:
	case SDL_SCANCODE_KP_2:
		for (loop=0; loop<count; loop++) {
			if (menu_focus < (menu_items - 1)) {
				menu_focus++;
				item = GetItem(menu_focus);
				item->SetFocus();
			}
			if ((menu_focus - menu_top) >= (MENUITEM_LINES - 3)) {
				if ((menu_top + MENUITEM_LINES - 2) < (menu_items - 1)) {
					menu_top++;
				}
			}
		}
		break;

	case SDL_SCANCODE_RIGHT:
	case SDL_SCANCODE_KP_6:
		item = GetItem(menu_focus);
		if (item->GetType() == MenuItem::SliderItem) {
			item->UpDownSlider(true);
			item = GetItem(menu_focus);
			menu->Command(false, item->GetID());
		}
		break;

	case SDL_SCANCODE_LEFT:
	case SDL_SCANCODE_KP_4:
		item = GetItem(menu_focus);
		if (item->GetType() == MenuItem::SliderItem) {
			item->UpDownSlider(false);
			item = GetItem(menu_focus);
			menu->Command(false, item->GetID());
		}
		break;

	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
		if (e->key.repeat == 0) {
			item = GetItem(menu_focus);
			menu->Command(false, item->GetID());
		}
		break;

	default:
		break;
	}
}

//
// OnMouseMotion()
// mouse motion event
//
void MenuList::OnMouseMotion(SDL_Event *e)
{
	int x;
	int y;
	int sy;
	MenuItem *item;
	SDL_Event evt;
	Uint32 button;
	Uint32 tick;

	// true mouse device only
	if (e->motion.which == SDL_TOUCH_MOUSEID) {
		return;
	}

	// convert point
	x = e->motion.x;
	y = e->motion.y;
	if (video->ConvertPoint(&x, &y) == false) {
		return;
	}

	// save screen  y
	sy = y;

	// get button state
	button = SDL_GetMouseState(NULL, NULL);

	// position to item
	if (PosToItem(&x, &y) == false) {
		// check button
		if ((button & SDL_BUTTON_LMASK) == 0) {
			return;
		}

		// interval (repeat)
		tick = SDL_GetTicks();
		if ((tick & MENU_MOTION_TICK_MASK) == (menu_tick & MENU_MOTION_TICK_MASK)) {
			return;
		}
		menu_tick = tick;

		if (IsUp(sy) == true) {
			evt.key.repeat = 0;
			evt.key.keysym.sym = SDLK_UP;
			evt.key.keysym.scancode = SDL_SCANCODE_UP;
			OnKeyDown(&evt);
		}
		if (IsDown(sy) == true) {
			evt.key.repeat = 0;
			evt.key.keysym.sym = SDLK_DOWN;
			evt.key.keysym.scancode = SDL_SCANCODE_DOWN;
			OnKeyDown(&evt);
		}
		return;
	}

	// set focus
	menu_focus = menu_top + y;
	item = GetItem(menu_focus);
	item->SetFocus();

	// slider
	if ((button & SDL_BUTTON_LMASK) != 0) {
		if (item->GetType() == MenuItem::SliderItem) {
			item->SetSlider(x, MENUITEM_WIDTH);
			menu->Command(true, item->GetID());
		}
	}
}

//
// OnMouseButtonDown()
// mouse button down event
//
void MenuList::OnMouseButtonDown(SDL_Event *e)
{
	int x;
	int y;
	int sy;
	MenuItem *item;
	SDL_Event evt;

	// true mouse device only
	if (e->motion.which == SDL_TOUCH_MOUSEID) {
		return;
	}

	// left button?
	if (e->button.button != SDL_BUTTON_LEFT) {
		return;
	}

	// left button ?
	if (e->button.state == SDL_PRESSED) {
		// left button -> save for ProcessMenu()
		mouse_down = true;
	}

	// convert point
	x = e->button.x;
	y = e->button.y;
	if (video->ConvertPoint(&x, &y) == false) {
		return;
	}

	// save screen y
	sy = y;

	// position to item
	if (PosToItem(&x, &y) == false) {
		if (IsUp(sy) == true) {
			evt.key.repeat = 0;
			evt.key.keysym.sym = SDLK_UP;
			evt.key.keysym.scancode = SDL_SCANCODE_UP;
			OnKeyDown(&evt);
		}
		if (IsDown(sy) == true) {
			evt.key.repeat = 0;
			evt.key.keysym.sym = SDLK_DOWN;
			evt.key.keysym.scancode = SDL_SCANCODE_DOWN;
			OnKeyDown(&evt);
		}
		return;
	}

	// get item
	menu_focus = menu_top + y;
	item = GetItem(menu_focus);
	item->SetFocus();

	// slider
	if (item->GetType() == MenuItem::SliderItem) {
		item->SetSlider(x, MENUITEM_WIDTH);
	}

	// command
	menu->Command(true, item->GetID());
}

//
// OnMouseButtonUp()
// mouse button up event
//
void MenuList::OnMouseButtonUp(SDL_Event *e)
{
	int x;
	int y;
	int focus;
	MenuItem *item;

	// true mouse device only
	if (e->motion.which == SDL_TOUCH_MOUSEID) {
		return;
	}

	// MENU_ENTER_THRES ? (release mouse after entering menu immediately)
	if ((Uint32)(SDL_GetTicks() - mouse_tick) < MENU_ENTER_THRES) {
		return;
	}

	// right button ?
	if ((e->button.button == SDL_BUTTON_RIGHT) && (e->button.state == SDL_RELEASED)) {
		// right button -> back
		menu->Command(false, MENU_BACK);
		return;
	}

	// x1 button ?
	if ((e->button.button == SDL_BUTTON_X1) && (e->button.state == SDL_RELEASED)) {
		if (menu_file == true) {
			// file menu -> directory up
			menu->Command(false, MENU_FILE_MIN);
		} else {
			// other -> menu back
			menu->Command(false, MENU_BACK);
		}
		return;
	}

	if (e->button.button != SDL_BUTTON_LEFT) {
		return;
	}
 
	// convert point
	x = e->button.x;
	y = e->button.y;
	if (video->ConvertPoint(&x, &y) == false) {
		return;
	}

	// position to item
	if (PosToItem(&x, &y) == false) {
		return;
	}

	// compare focus item
	focus = menu_top + y;
	if (focus == menu_focus) {
		// get item and command
		item = GetItem(menu_focus);
		menu->Command(false, item->GetID());
	}
}

//
// OnMouseWheel()
// mouse wheel event
//
void MenuList::OnMouseWheel(SDL_Event *e)
{
	SDL_Event evt;
	int loop;
	int x;
	int y;
	MenuItem *item;
	bool inside;

	// true mouse device only
	if (e->wheel.which == SDL_TOUCH_MOUSEID) {
		return;
	}

	// get current position
	x = 0;
	y = 0;
	SDL_GetMouseState(&x, &y);

	// convert point
	inside = false;
	if (video->ConvertPoint(&x, &y) == true) {
		if (PosToItem(&x, &y) == true) {
			inside = true;
		}
	}

	// init
	evt.key.repeat = 0;
	evt.key.keysym.scancode = SDL_SCANCODE_UNKNOWN;

	// up
	if (e->wheel.y > 0) {
		evt.key.keysym.sym = SDLK_UP;
		evt.key.keysym.scancode = SDL_SCANCODE_UP;
		for (loop=0; loop<e->wheel.y; loop++) {
			if (inside == true) {
				if (menu_top > 0) {
					menu_top--;
					continue;
				}
			}
			OnKeyDown(&evt);
		}
	}

	// down
	if (e->wheel.y < 0) {
		evt.key.keysym.sym = SDLK_DOWN;
		evt.key.keysym.scancode = SDL_SCANCODE_DOWN;
		for (loop=0; loop<(-e->wheel.y); loop++) {
			if (inside == true) {
				if ((menu_top + MENUITEM_LINES - 2) < (menu_items - 1)) {
					menu_top++;
					continue;
				}
			}
			OnKeyDown(&evt);
		}
	}

	// position to item
	if (inside == true) {
		menu_focus = menu_top + y;
		if (menu_focus >= menu_items) {
			menu_focus = menu_items - 1;
		}
	}
	else {
		if (menu_focus < menu_top) {
			menu_focus = menu_top;
		}
		else {
			if (menu_focus > (menu_top + MENUITEM_LINES - 2)) {
				menu_focus = menu_top + MENUITEM_LINES - 2;
				if (menu_focus >= menu_items) {
					menu_focus = menu_items - 1;
				}
			}
		}
	}

	// set focus
	item = GetItem(menu_focus);
	item->SetFocus();
}

//
// OnJoystick()
// joystick event
//
void MenuList::OnJoystick()
{
	Uint32 status[2];
	SDL_Event evt;

	// get axis and button
	input->GetJoystick(status);

	// disable repeat if button is down
	if ((status[0] & 0x30) != 0) {
		// not repeat for button
		if ((joy_prev & 0x30) != 0) {
			return;
		}
	}

	// init
	evt.key.repeat = 0;
	evt.key.keysym.scancode = SDL_SCANCODE_UNKNOWN;

	// up
	if ((status[0] & 0x01) != 0) {
		evt.key.keysym.sym = SDLK_UP;
		evt.key.keysym.scancode = SDL_SCANCODE_UP;
	}

	// down
	if ((status[0] & 0x02) != 0) {
		evt.key.keysym.sym = SDLK_DOWN;
		evt.key.keysym.scancode = SDL_SCANCODE_DOWN;
	}

	// left
	if ((status[0] & 0x04) != 0) {
		evt.key.keysym.sym = SDLK_LEFT;
		evt.key.keysym.scancode = SDL_SCANCODE_LEFT;
	}

	// right
	if ((status[0] & 0x08) != 0) {
		evt.key.keysym.sym = SDLK_RIGHT;
		evt.key.keysym.scancode = SDL_SCANCODE_RIGHT;
	}

	// button 0
	if ((status[0] & 0x10) != 0) {
		evt.key.keysym.sym = SDLK_RETURN;
		evt.key.keysym.scancode = SDL_SCANCODE_RETURN;
	}

	// button 1
	if ((status[0] & 0x20) != 0) {
		evt.key.keysym.sym = SDLK_ESCAPE;
		evt.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
	}

	// emulate key down
	if (evt.key.keysym.scancode != SDL_SCANCODE_UNKNOWN) {
		if ((joy_prev & 0x0f) == 0) {
			// first key down
			OnKeyDown(&evt);

			joy_tick = SDL_GetTicks();
			joy_diff = MENU_JOY_FIRST;
		}
		else {
			// repeat
			if ((Uint32)(SDL_GetTicks() - joy_tick) >= joy_diff) {
				// repeat key down
				OnKeyDown(&evt);

				joy_diff += MENU_JOY_REPEAT;
			}
		}
	}

	// save joy_prev
	joy_prev = (status[0] & 0x3f);
}

//
// OnFingerDown()
// finger down
//
void MenuList::OnFingerDown(SDL_Event *e)
{
	int x;
	int y;
	MenuItem *item;
	bool result;

	// save finger_x, finger_y
	finger_x = -1;
	finger_y = -1;
	video->ConvertFinger(e->tfinger.x, e->tfinger.y, &finger_x, &finger_y);

	// save tick
	finger_tick = SDL_GetTicks();
	finger_counter = SDL_GetPerformanceCounter();
	finger_tap_valid = true;
	finger_focus = -1;

	// finger position to item
	x = 0;
	y = 0;
	result = FingerToItem(e->tfinger.x, e->tfinger.y, &x, &y);

	// check result
	if (result == false) {
		return;
	}

	// get item
	menu_focus = menu_top + y;
	item = GetItem(menu_focus);
	item->SetFocus();
	finger_focus = menu_focus;
	finger_slider = false;

	// command
	menu->Command(true, item->GetID());
}

//
// OnFingerUp()
// finger up
//
void MenuList::OnFingerUp(SDL_Event *e)
{
	int x;
	int y;
	int focus;
	MenuItem *item;
	bool result;

	// finger position to item
	x = 0;
	y = 0;
	result = FingerToItem(e->tfinger.x, e->tfinger.y, &x, &y);

	bool cancel_tap = !finger_tap_valid || finger_counter == 0;
#ifndef __ANDROID__
	const Uint64 frequency = SDL_GetPerformanceFrequency();
	const Uint64 elapsed = SDL_GetPerformanceCounter() - finger_counter;
	const Uint64 maximum = (frequency * FINGER_TIME_THRES) / 1000;
	cancel_tap = cancel_tap || (frequency != 0 && elapsed > maximum);
#endif
	if (cancel_tap) {
		// version 1.70
		finger_focus = -1;
		finger_slider = false;
		finger_tap_valid = false;
		return;
	}

	// outside area -> back
	if ((result == false) && (finger_focus < 0)) {
		menu->CmdBack();
		return;
	}

	// compare focus item
	focus = menu_top + y;
	if (focus == menu_focus) {
		// get item and command
		item = GetItem(menu_focus);
		menu->Command(false, item->GetID());
	}

	// clear finger_focus
	finger_focus = -1;
	finger_slider = false;
	finger_tap_valid = false;
}

//
// OnFingerMotion()
// finger motion
//
void MenuList::OnFingerMotion(SDL_Event *e)
{
	int fx;
	int fy;
	int x;
	int y;
	MenuItem *item;

	// convert position
	fx = -1;
	fy = -1;
	video->ConvertFinger(e->tfinger.x, e->tfinger.y, &fx, &fy);

	// finger position to item
	x = 0;
	y = 0;
	if (FingerToItem(e->tfinger.x, e->tfinger.y, &x, &y) == false) {
		if (menu_focus == (menu_items - 1)) {
			// spread if only one item is shown
			if (IsLow(x, y) == true) {
				if (menu_top > 0) {
					menu_top--;
					finger_tap_valid = false;

					// dec tick to avoid command (version 1.70)
					finger_tick = (Uint32)(SDL_GetTicks() - FINGER_TIME_THRES - 1);
				}

				item = GetItem(menu_focus);
				item->SetFocus();
			}
		}
		return;
	}

	// scroll
	if (finger_slider == false) {
		// dec tick to avoid command (version 1.70)
		if (menu_top != (menu_focus - y)) {
			finger_tap_valid = false;
			finger_tick = (Uint32)(SDL_GetTicks() - FINGER_TIME_THRES - 1);
		}

		menu_top = (menu_focus - y);
		if (menu_top < 0) {
			menu_top = 0;
		}
	}

	item = GetItem(menu_focus);
	item->SetFocus();

	// slider
	if (item->GetType() == MenuItem::SliderItem) {
		if (finger_slider == false) {
			// slider move is disabled (default)
			if ((Uint32)(SDL_GetTicks() - finger_tick) >= FINGER_SLIDER_THRES) {
				// keep in touch
				if (finger_focus == menu_focus) {
					// focus is as same as on touch
					if ((fx < (finger_x - FINGER_POS_THRES)) || (fx >(finger_x + FINGER_POS_THRES))) {
						// move over FINGER_POS_THRES
						finger_slider = true;
						finger_tap_valid = false;
					}
				}
			}
		}

		if (finger_slider == true) {
			item->SetSlider(x, MENUITEM_WIDTH);
			menu->Command(true, item->GetID());
		}
	}
}

//
// FingerToItem()
// finger position to item
//
bool MenuList::FingerToItem(float tx, float ty, int *x, int *y)
{
	// convert x, y
	if (video->ConvertFinger(tx, ty, x, y) == false) {
		return false;
	}

	return PosToItem(x, y, true);
}

//
// PosToItem()
// mouse position to item
//
bool MenuList::PosToItem(int *x, int *y, bool finger)
{
	int xx;
	int yy;
	int mod;

	// get
	xx = *x;
	yy = *y;

	// check x
	if (xx < ((SCREEN_WIDTH / 2)- (MENUITEM_WIDTH / 2))) {
		return false;
	}
	if (xx > ((SCREEN_WIDTH / 2) + (MENUITEM_WIDTH / 2))) {
		return false;
	}

	// xx
	xx -= ((SCREEN_WIDTH / 2) - (MENUITEM_WIDTH / 2));

	// check y
	yy -= ((SCREEN_HEIGHT / 2) - ((MENUITEM_HEIGHT * MENUITEM_LINES) / 2));
	if (yy < 0) {
		return false;
	}

	// divide
	mod = yy % MENUITEM_HEIGHT;
	yy /= MENUITEM_HEIGHT;

	// title ?
	if (yy == 0) {
		return false;
	}
	if (yy >= MENUITEM_LINES) {
		// version 1.70
		if ((yy == MENUITEM_LINES) && (finger == true)) {
			// mergin:1/3 * MENUITEM_HEIGHT
			if (mod < (MENUITEM_HEIGHT / 3)) {
				yy = MENUITEM_LINES - 1;
			}
		}

		if (yy >= MENUITEM_LINES) {
			return false;
		}
	}
	yy--;

	// over ?
	if ((menu_top + yy) >= menu_items) {
		// version 1.70
		if ((menu_top + yy == menu_items) && (finger == true)) {
			// mergin:1/3 * MENUITEM_HEIGHT
			if (mod < (MENUITEM_HEIGHT / 3)) {
				yy = menu_items - menu_top - 1;
			}
		}

		if ((menu_top + yy) >= menu_items) {
			return false;
		}
	}

	// ok
	*x = xx;
	*y = yy;
	return true;
}

//
// IsUp()
// check screen position to up scroll
//
bool MenuList::IsUp(int y)
{
	// check y
	if (y <= (SCREEN_HEIGHT / 2)) {
		if (menu_focus > 0) {
			return true;
		}
	}

	return false;
}

//
// IsDown()
// check screen position to down scroll
//
bool MenuList::IsDown(int y)
{
	// check y
	if (y > (SCREEN_HEIGHT / 2)) {
		if (menu_focus < (menu_items - 1)) {
			return true;
		}
	}

	return false;
}

//
// IsLow()
// check screen position to spread area
//
bool MenuList::IsLow(int x, int y)
{
	// check x
	if (x < ((SCREEN_WIDTH / 2)- (MENUITEM_WIDTH / 2))) {
		return false;
	}
	if (x >= ((SCREEN_WIDTH / 2) + (MENUITEM_WIDTH / 2))) {
		return false;
	}

	// x
	x -= ((SCREEN_WIDTH / 2) - (MENUITEM_WIDTH / 2));

	// check y
	y -= ((SCREEN_HEIGHT / 2) - ((MENUITEM_HEIGHT * MENUITEM_LINES) / 2));
	if (y < 0) {
		return false;
	}

	// divide
	y /= MENUITEM_HEIGHT;

	// title ?
	if (y == 0) {
		return false;
	}
	if (y >= MENUITEM_LINES) {
		return false;
	}

	// ok
	return true;
}

#endif // SDL
