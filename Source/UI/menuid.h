//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ menu id ]
//

#ifdef SDL

#ifndef MENUID_H
#define MENUID_H

//
// global id
//
#define MENU_BACK                       0
										// back to upper menu

//
// menu id
//
#define MENU_MAIN                       1
										// main menu
#define MENU_DRIVE1                     2
										// drive1 menu
#define MENU_DRIVE2                     3
										// drive2 menu
#define MENU_CMT                        4
										// cmt menu
#define MENU_LOAD                       5
										// load menu
#define MENU_SAVE                       6
										// save menu
#define MENU_SYSTEM                     7
										// system menu
#define MENU_VIDEO                      8
										// video menu
#define MENU_AUDIO                      9
										// audio menu
#define MENU_AUDIO_OUT                  10
										// audio output menu
#define MENU_INPUT                      11
										// input menu
#define MENU_RESET                      12
										// reset menu
#define MENU_QUIT                       13
										// quit menu
#define MENU_SOFTKEY1                   14
										// softkey menu 1
#define MENU_SOFTKEY2                   15
										// softkey menu 2
#define MENU_SOFTKEY3                   16
										// softkey menu 3
#define MENU_SOFTKEY4                   17
										// softkey menu 4
#define MENU_DIP                        18
										// dipswitch menu
#define MENU_JOYMAP                     19
										// joystick to keyboard map menu
#define MENU_VMKEY                      20
										// vmkey menu
#define MENU_FILE                       21
										// file menu
#define MENU_JOYTEST                    22
										// joystick test menu
#define MENU_SCALEFILTER                23
										// video scale filter
#define MENU_RA                         24
										// RetroAchievements menu
#define MENU_RA_LIBRARY_VIEW            25
										// RetroAchievements library menu
#define MENU_RA_GAME_DETAIL             26
										// RetroAchievements game detail menu
#define MENU_RA_ACHIEVEMENTS_VIEW       27
										// RetroAchievements achievements menu
#define MENU_RA_ACHIEVEMENT_DETAIL      28
										// RetroAchievements achievement detail menu
#define MENU_RA_LEADERBOARDS_VIEW       29
										// RetroAchievements leaderboards menu
#define MENU_PLAYLIST                   30
//
// main menu
//
#define MENU_MAIN_MIN                   100
										// minimum
#define MENU_MAIN_DRIVE1                100
										// drive 1
#define MENU_MAIN_DRIVE2                101
										// drive 2
#define MENU_MAIN_CMT                   102
										// cmt
#define MENU_MAIN_LOAD                  103
										// load state
#define MENU_MAIN_SAVE                  104
										// save state
#define MENU_MAIN_SYSTEM                105
										// system options
#define MENU_MAIN_VIDEO                 106
										// video options
#define MENU_MAIN_AUDIO                 107
										// audio options
#define MENU_MAIN_AUDIO_OUT             108
										// audio output device
#define MENU_MAIN_INPUT                 109
										// input options
#define MENU_MAIN_RA                    110
										// RetroAchievements
#define MENU_MAIN_SCREEN                111
										// full/window screen
#define MENU_MAIN_SPEED                 112
										// full/normal speed
#define MENU_MAIN_RESET                 113
										// reset
#define MENU_MAIN_QUIT                  114
										// quit
#define MENU_MAIN_SCREENSHOT             115
#define MENU_MAIN_SCREENSHOT_FOLDER      116
#define MENU_MAIN_MAX                   199
										// maximum

//
// drive1 menu
//
#define MENU_DRIVE1_MIN                 200
										// minimum
#define MENU_DRIVE1_BANK0               200
										// bank 0
#define MENU_DRIVE1_OPEN                264
										// open
#define MENU_DRIVE1_BOTH                265
										// open drive1 & drive2
#define MENU_DRIVE1_EJECT               266
										// eject
#define MENU_DRIVE1_MAX                 299
										// maximum

//
// drive2 menu
//
#define MENU_DRIVE2_MIN                 300
										// minimum
#define MENU_DRIVE2_BANK0               300
										// bank 0
#define MENU_DRIVE2_OPEN                364
										// open
#define MENU_DRIVE2_BOTH                365
										// open drive1 & drive2
#define MENU_DRIVE2_EJECT               366
										// eject
#define MENU_DRIVE2_MAX                 399
										// maximum

//
// cmt menu
//
#define MENU_CMT_MIN                    400
										// minimum
#define MENU_CMT_PLAY                   401
										// play
#define MENU_CMT_REC                    402
										// rec
#define MENU_CMT_EJECT                  403
										// eject
#define MENU_CMT_MAX                    499
										// maximum

//
// load menu
//
#define MENU_LOAD_MIN                   500
										// minimum
#define MENU_LOAD_0                     500
										// slot 0
#define MENU_LOAD_1                     501
										// slot 1
#define MENU_LOAD_2                     502
										// slot 2
#define MENU_LOAD_3                     503
										// slot 3
#define MENU_LOAD_4                     504
										// slot 4
#define MENU_LOAD_5                     505
										// slot 5
#define MENU_LOAD_6                     506
										// slot 6
#define MENU_LOAD_7                     507
										// slot 7
#define MENU_LOAD_8                     508
										// slot 8
#define MENU_LOAD_9                     509
										// slot 9
#define MENU_LOAD_MAX                   599
										// maximum

//
// save menu
//
#define MENU_SAVE_MIN                   600
										// minimum
#define MENU_SAVE_0                     600
										// slot 0
#define MENU_SAVE_1                     601
										// slot 1
#define MENU_SAVE_2                     602
										// slot 2
#define MENU_SAVE_3                     603
										// slot 3
#define MENU_SAVE_4                     604
										// slot 4
#define MENU_SAVE_5                     605
										// slot 5
#define MENU_SAVE_6                     606
										// slot 6
#define MENU_SAVE_7                     607
										// slot 7
#define MENU_SAVE_8                     608
										// slot 8
#define MENU_SAVE_9                     609
										// slot 9
#define MENU_SAVE_MAX                   699
										// maximum

//
// system menu
//
#define MENU_SYSTEM_MIN                 700
										// minimum
#define MENU_SYSTEM_V1S                 700
										// V1S mode
#define MENU_SYSTEM_V1H                 701
										// V1H mode
#define MENU_SYSTEM_V2                  702
										// V2 mode
#define MENU_SYSTEM_N                   703
										// N mode
#define MENU_SYSTEM_4M                  704
										// clock 4MHz
#define MENU_SYSTEM_8M                  705
										// clock 8MHz
#define MENU_SYSTEM_8MH                 706
										// clock 8MHz (FE2/MC)
#define MENU_SYSTEM_EXRAM               707
										// extended 128KB RAM
#define MENU_SYSTEM_FASTDISK            708
										// pseudo fast disk access
#define MENU_SYSTEM_BATTERY             709
										// watch battery level
#define MENU_SYSTEM_DIP                 710
										// dip switch
#define MENU_SYSTEM_ROMVER              711
										// rom version
#define MENU_ANDROID_SAF                712
										// storage access framework (for Android 5.0 or later)
#define MENU_SYSTEM_MODE                797
										// mode radio
#define MENU_SYSTEM_CLOCK               798
										// clock radio
#define MENU_SYSTEM_MAX                 799
										// maximum

//
// video menu
//
#define MENU_VIDEO_MIN                  800
										// minimum
#define MENU_VIDEO_640                  801
										// x1.0
#define MENU_VIDEO_960                  802
										// x1.5
#define MENU_VIDEO_1280                 803
										// x2.0
#define MENU_VIDEO_1600                 804
										// x2.5
#define MENU_VIDEO_1920                 805
										// x3.0
#define MENU_VIDEO_SKIP0                806
										// 0 frame skip (ex:60fps)
#define MENU_VIDEO_SKIP1                807
										// 1 frame skip (ex:30fps)
#define MENU_VIDEO_SKIP2                808
										// 2 frame skip (ex:20fps)
#define MENU_VIDEO_SKIP3                809
										// 3 frame skip (ex:15fps)
#define MENU_VIDEO_15K                  810
										// 15kHz monitor
#define MENU_VIDEO_24K                  811
										// 24kHz monitor
#define MENU_VIDEO_SCANLINE             812
										// scanline
#define MENU_VIDEO_BRIGHTNESS           813
										// brightness
#define MENU_VIDEO_STATUSCHK            814
										// status line
#define MENU_VIDEO_STATUSALPHA          815
										// status alpha
#define MENU_VIDEO_SCALEFILTER          816
										// scaling filter
#define MENU_VIDEO_FORCERGB565          817
										// force RGB565
#define MENU_VIDEO_ROTATE_AUTO           818
										// auto rotation
#define MENU_VIDEO_ROTATE_LANDSCAPE      819
										// landscape rotation
#define MENU_VIDEO_ROTATE_PORTRAIT       820
										// portrait rotation
#define MENU_VIDEO_ROTATION              895
										// rotation radio
#define MENU_VIDEO_WINDOW               896
										// window radio
#define MENU_VIDEO_SKIP                 897
										// skip radio
#define MENU_VIDEO_MONITOR              898
										// monitor radio
#define MENU_VIDEO_MAX                  899
										// maximum

//
// audio menu
//
#define MENU_AUDIO_MIN                  900
										// minimum
#define MENU_AUDIO_44100                901
										// 44100Hz
#define MENU_AUDIO_48000                902
										// 48000Hz
#define MENU_AUDIO_55467                903
										// 55467Hz
#define MENU_AUDIO_88200                904
										// 88200Hz
#define MENU_AUDIO_96000                905
										// 96000Hz
#define MENU_AUDIO_BUFFER               906
										// buffer
#define MENU_AUDIO_OPN                  907
										// YM2203 (SR/FR/TR/MR/FH/MH/FE/FE2/VA/DO)
#define MENU_AUDIO_OPNA                 908
										// YM2608 (FA/MA/MA2/MC/VA2/VA3/DO+)
#define MENU_AUDIO_DEVICE               996
										// audio device
#define MENU_AUDIO_FREQ                 997
										// frequency radio
#define MENU_AUDIO_SBII                 998
										// sound board II radio
#define MENU_AUDIO_MAX                  999
										// maximum

//
// input menu
//
#define MENU_INPUT_MIN                  1000
										// minimum
#define MENU_INPUT_SOFTKEY1             1001
										// softkey 1
#define MENU_INPUT_SOFTKEY2             1002
										// softkey 2
#define MENU_INPUT_SOFTKEY3             1003
										// softkey 3
#define MENU_INPUT_SOFTKEY4             1004
										// softkey 4
#define MENU_INPUT_SOFTBRI              1005
										// softkey brightness
#define MENU_INPUT_SOFTALPHA            1006
										// softkey alpha
#define MENU_INPUT_SOFTTIME             1007
										// softkey timeout
#define MENU_INPUT_KEYENABLE            1008
										// keyboard enable
#define MENU_INPUT_JOYENABLE            1009
										// joystick enable
#define MENU_INPUT_JOYSWAP              1010
										// joystick button swap
#define MENU_INPUT_JOYKEY               1011
										// joystick to keyboard
#define MENU_INPUT_JOYMAP               1012
										// joystick to keyboard map
#define MENU_INPUT_MOUSETIME            1013
										// mouse timeout
#define MENU_INPUT_JOYTEST              1014
										// joystick test
#define MENU_INPUT_SOFTKEY_MM           1015
										// show softkey when mouse move enable
#define MENU_INPUT_CURSOR_NUM           1016
										// cursor to num pad
#define MENU_INPUT_NUM_PAD              1017
										// num to num pad
#define MENU_INPUT_MAX                  1099
										// maximum

//
// reset menu
//
#define MENU_RESET_MIN                  1100
										// minimum
#define MENU_RESET_YES                  1101
										// yes
#define MENU_RESET_NO                   1102
										// no
#define MENU_RESET_MAX                  1199
										// maximum

//
// quit menu
//
#define MENU_QUIT_MIN                   1200
										// minimum
#define MENU_QUIT_YES                   1201
										// yes
#define MENU_QUIT_NO                    1202
										// no
#define MENU_QUIT_MAX                   1299
										// maximum

//
// softkey menu
//
#define MENU_SOFTKEY_MIN                1300
										// minimum
#define MENU_SOFTKEY_0                  1300
										// softkey type 0
#define MENU_SOFTKEY_1                  1301
										// softkey type 1
#define MENU_SOFTKEY_2                  1302
										// softkey type 2
#define MENU_SOFTKEY_3                  1303
										// softkey type 3
#define MENU_SOFTKEY_4                  1304
										// softkey type 4
#define MENU_SOFTKEY_5                  1305
										// softkey type 5
#define MENU_SOFTKEY_6                  1306
										// softkey type 6
#define MENU_SOFTKEY_7                  1307
										// softkey type 7
#define MENU_SOFTKEY_8                  1308
										// softkey type 8
#define MENU_SOFTKEY_9                  1309
										// softkey type 9
#define MENU_SOFTKEY_10                 1310
										// softkey type 10
#define MENU_SOFTKEY_11                 1311
										// softkey type 11
#define MENU_SOFTKEY_MAX                1399
										// maximum

//
// dipswitch menu
//
#define MENU_DIP_MIN                    1400
										// minimum
#define MENU_DIP_BASICMODE              1401
										// basic mode (default)
#define MENU_DIP_TERMMODE               1402
										// terminal mode
#define MENU_DIP_WIDTH80                1403
										// width 80 (default)
#define MENU_DIP_WIDTH40                1404
										// width 40
#define MENU_DIP_LINE20                 1405
										// line 20 (default)
#define MENU_DIP_LINE25                 1406
										// line 25
#define MENU_DIP_FROMDISK               1407
										// boot from disk (default)
#define MENU_DIP_FROMROM                1408
										// boot from rom
#define MENU_DIP_MEMWAIT_OFF            1409
										// memory wait off (default)
#define MENU_DIP_MEMWAIT_ON             1410
										// memory wait on
#define MENU_DIP_BAUD75                 1411
										// baudrate 75bps
#define MENU_DIP_BAUD150                1412
										// baudrate 150bps
#define MENU_DIP_BAUD300                1413
										// baudrate 300bps
#define MENU_DIP_BAUD600                1414
										// baudrate 600bps
#define MENU_DIP_BAUD1200               1415
										// baudrate 1200bps
#define MENU_DIP_BAUD2400               1416
										// baudrate 2400bps
#define MENU_DIP_BAUD4800               1417
										// baudrate 4800bps
#define MENU_DIP_BAUD9600               1418
										// baudrate 9600bps
#define MENU_DIP_BAUD19200              1419
										// baudrate 19200bps
#define MENU_DIP_HALFDUPLEX             1420
										// half duplex
#define MENU_DIP_FULLDUPLEX             1421
										// full duplex (default)
#define MENU_DIP_DATA8BIT               1422
										// databit 8bit (default)
#define MENU_DIP_DATA7BIT               1423
										// databit 7bit
#define MENU_DIP_STOP2BIT               1424
										// stopbit 2bit
#define MENU_DIP_STOP1BIT               1425
										// stopbit 1bit (default)
#define MENU_DIP_XON                    1426
										// enable X parameter (default)
#define MENU_DIP_XOFF                   1427
										// disable X parameter
#define MENU_DIP_SON                    1428
										// enable S parameter
#define MENU_DIP_SOFF                   1429
										// disable S parameter (default)
#define MENU_DIP_DELON                  1430
										// enable DEL code (default)
#define MENU_DIP_DELOFF                 1431
										// disable DEL code (default)
#define MENU_DIP_NOPARITY               1432
										// no parity (default)
#define MENU_DIP_EVENPARITY             1433
										// even parity
#define MENU_DIP_ODDPARITY              1434
										// odd parity
#define MENU_DIP_DEFAULT                1435
										// restore default settings
#define MENU_DIP_BOOTMODE               1486
										// boot mode radio
#define MENU_DIP_WIDTH                  1487
										// width radio
#define MENU_DIP_LINE                   1488
										// line radio
#define MENU_DIP_BOOTFROM               1489
										// boot from radio
#define MENU_DIP_MEMWAIT                1490
										// memory wait radio
#define MENU_DIP_BAUDRATE               1491
										// baudrate radio
#define MENU_DIP_DUPLEX                 1492
										// duplex radio
#define MENU_DIP_DATABIT                1493
										// databit radio
#define MENU_DIP_STOPBIT                1494
										// stopbit radio
#define MENU_DIP_X                      1495
										// X parameter radio
#define MENU_DIP_S                      1496
										// S parameter radio
#define MENU_DIP_DEL                    1497
										// DEL code radio
#define MENU_DIP_PARITY                 1498
										// parity radio
#define MENU_DIP_MAX                    1499
										// maximum

//
// joystick to keyboard map menu
//
#define MENU_JOYMAP_MIN                 1500
										// minimum
#define MENU_JOYMAP_DPAD_UP             1501
										// SDL_CONTROLLER_BUTTON_DPAD_UP
#define MENU_JOYMAP_DPAD_DOWN           1502
										// SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define MENU_JOYMAP_DPAD_LEFT           1503
										// SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define MENU_JOYMAP_DPAD_RIGHT          1504
										// SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define MENU_JOYMAP_A                   1505
										// SDL_CONTROLLER_BUTTON_A
#define MENU_JOYMAP_B                   1506
										// SDL_CONTROLLER_BUTTON_B
#define MENU_JOYMAP_X                   1507
										// SDL_CONTROLLER_BUTTON_X
#define MENU_JOYMAP_Y                   1508
										// SDL_CONTROLLER_BUTTON_Y
#define MENU_JOYMAP_BACK                1509
										// SDL_CONTROLLER_BUTTON_BACK
#define MENU_JOYMAP_GUIDE               1510
										// SDL_CONTROLLER_BUTTON_GUIDE
#define MENU_JOYMAP_START               1511
										// SDL_CONTROLLER_BUTTON_START
#define MENU_JOYMAP_LEFTSTICK           1512
										// SDL_CONTROLLER_BUTTON_LEFTSTICK
#define MENU_JOYMAP_RIGHTSTICK          1513
										// SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define MENU_JOYMAP_LEFTSLDR            1514
										// SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define MENU_JOYMAP_RIGHTSLDR           1515
										// SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define MENU_JOYMAP_DEFAULT             1516
										// restore default settings
#define MENU_JOYMAP_MAX                 1599
										// maximum

//
// vmkey menu
//
#define MENU_VMKEY_MIN                  1600
										// minumum
#define MENU_VMKEY_MENU                 1601
										// (Menu)
#define MENU_VMKEY_NEXT                 1602
										// (Next)
#define MENU_VMKEY_PREV                 1603
										// (Prev)
#define MENU_VMKEY_TEN0                 1604
										// Tenkey 0
#define MENU_VMKEY_TEN1                 1605
										// Tenkey 1
#define MENU_VMKEY_TEN2                 1606
										// Tenkey 2
#define MENU_VMKEY_TEN3                 1607
										// Tenkey 3
#define MENU_VMKEY_TEN4                 1608
										// Tenkey 4
#define MENU_VMKEY_TEN5                 1609
										// Tenkey 5
#define MENU_VMKEY_TEN6                 1610
										// Tenkey 6
#define MENU_VMKEY_TEN7                 1611
										// Tenkey 7
#define MENU_VMKEY_TEN8                 1612
										// Tenkey 8
#define MENU_VMKEY_TEN9                 1613
										// Tenkey 9
#define MENU_VMKEY_F1                   1614
										// Function 1
#define MENU_VMKEY_F2                   1615
										// Function 2
#define MENU_VMKEY_F3                   1616
										// Function 3
#define MENU_VMKEY_F4                   1617
										// Function 4
#define MENU_VMKEY_F5                   1618
										// Function 5
#define MENU_VMKEY_ESC                  1619
										// ESC
#define MENU_VMKEY_SPACE                1620
										// SPACE
#define MENU_VMKEY_RETURN               1621
										// RETURN
#define MENU_VMKEY_DEL                  1622
										// DEL
#define MENU_VMKEY_HOMECLR              1623
										// HOMECLR
#define MENU_VMKEY_HELP                 1624
										// HELP
#define MENU_VMKEY_SHIFT                1625
										// SHIFT
#define MENU_VMKEY_CTRL                 1626
										// CTRL
#define MENU_VMKEY_CAPS                 1627
										// CAPS
#define MENU_VMKEY_KANA                 1628
										// KANA
#define MENU_VMKEY_GRPH                 1629
										// GRPH
#define MENU_VMKEY_A                    1630
										// A
#define MENU_VMKEY_B                    1631
										// B
#define MENU_VMKEY_C                    1632
										// C
#define MENU_VMKEY_D                    1633
										// D
#define MENU_VMKEY_E                    1634
										// E
#define MENU_VMKEY_F                    1635
										// F
#define MENU_VMKEY_G                    1636
										// G
#define MENU_VMKEY_H                    1637
										// H
#define MENU_VMKEY_I                    1638
										// I
#define MENU_VMKEY_J                    1639
										// J
#define MENU_VMKEY_K                    1640
										// K
#define MENU_VMKEY_L                    1641
										// L
#define MENU_VMKEY_M                    1642
										// M
#define MENU_VMKEY_N                    1643
										// N
#define MENU_VMKEY_O                    1644
										// O
#define MENU_VMKEY_P                    1645
										// P
#define MENU_VMKEY_Q                    1646
										// Q
#define MENU_VMKEY_R                    1647
										// R
#define MENU_VMKEY_S                    1648
										// S
#define MENU_VMKEY_T                    1649
										// T
#define MENU_VMKEY_U                    1650
										// U
#define MENU_VMKEY_V                    1651
										// V
#define MENU_VMKEY_W                    1652
										// W
#define MENU_VMKEY_X                    1653
										// X
#define MENU_VMKEY_Y                    1654
										// Y
#define MENU_VMKEY_Z                    1655
										// Z
#define MENU_VMKEY_UP                   1656
										// UP
#define MENU_VMKEY_DOWN                 1657
										// DOWN
#define MENU_VMKEY_LEFT                 1658
										// LEFT
#define MENU_VMKEY_RIGHT                1659
										// RIGHT
#define MENU_VMKEY_ROLLUP               1660
										// ROLL UP
#define MENU_VMKEY_ROLLDOWN             1661
										// ROLL DOWN
#define MENU_VMKEY_TAB                  1662
										// TAB
#define MENU_VMKEY_MAX                  1699
										// maximum

//
// joystick test menu
//
#define MENU_JOYTEST_MIN                1700
										// minumum
#define MENU_JOYTEST_QUIT               1701
										// QUIT
#define MENU_JOYTEST_BUTTON1            1702
										// BUTTON 1
#define MENU_JOYTEST_BUTTON2            1703
										// BUTTON 2
#define MENU_JOYTEST_BUTTON3            1704
										// BUTTON 3
#define MENU_JOYTEST_BUTTON4            1705
										// BUTTON 4
#define MENU_JOYTEST_BUTTON5            1706
										// BUTTON 5
#define MENU_JOYTEST_BUTTON6            1707
										// BUTTON 6
#define MENU_JOYTEST_BUTTON7            1708
										// BUTTON 7
#define MENU_JOYTEST_BUTTON8            1709
										// BUTTON 8
#define MENU_JOYTEST_BUTTON9            1710
										// BUTTON 9
#define MENU_JOYTEST_BUTTON10           1711
										// BUTTON 10
#define MENU_JOYTEST_BUTTON11           1712
										// BUTTON 11
#define MENU_JOYTEST_BUTTON12           1713
										// BUTTON 12
#define MENU_JOYTEST_BUTTON13           1714
										// BUTTON 13
#define MENU_JOYTEST_BUTTON14           1715
										// BUTTON 14
#define MENU_JOYTEST_BUTTON15           1716
										// BUTTON 15
#define MENU_JOYTEST_MAX                1799
										// maximum

#define MENU_AUDIO_DEVICE_MIN           1800
										// minimum
#define MENU_AUDIO_DEVICE_MAX           1899
										// maximum

//
// video scaling filter menu
//
#define MENU_SCALEFILTER_MIN            1900
#define MENU_SCALEFILTER_NEAREST        1901
#define MENU_SCALEFILTER_LINEAR         1902
#define MENU_SCALEFILTER_ANISOTROPIC    1903
#define MENU_SCALEFILTER_XBRZ           1904
#define MENU_SCALEFILTER_MAX            1999

//
// RetroAchievements menu
//
#define MENU_RA_MIN                     2000
										// minimum
#define MENU_RA_MODE                    2001
										// RetroAchievements mode
#define MENU_RA_STATUS                  2002
										// RetroAchievements status
#define MENU_RA_LOGIN                   2003
										// RetroAchievements login/logout
#define MENU_RA_LIBRARY                 2004
										// RetroAchievements library
#define MENU_RA_ACHIEVEMENTS            2005
										// RetroAchievements achievements
#define MENU_RA_LEADERBOARDS            2006
										// RetroAchievements leaderboards
#define MENU_RA_PRESENCE                2007
										// RetroAchievements Rich Presence
#define MENU_RA_LOAD                    2008
										// RetroAchievements load state
#define MENU_RA_SAVE                    2009
										// RetroAchievements save state
#define MENU_RA_HARDCORE                2010
										// RetroAchievements Casual/Hardcore
#define MENU_RA_HARDCORE_YES            2011
										// confirm leaving Hardcore
#define MENU_RA_HARDCORE_NO             2012
										// cancel leaving Hardcore
#define MENU_RA_WEBSITE                 2013
										// RetroAchievements website
#define MENU_RA_GAME_START              2014
										// RetroAchievements game detail action
#define MENU_RA_LOAD_HARDCORE_DEBUG     2015
										// load a Hardcore-created state in Casual
#define MENU_RA_LOGOUT_YES              2016
										// confirm deleting pending unlocks and logout
#define MENU_RA_LOGOUT_NO               2017
										// cancel logout
#define MENU_RA_PRIVACY                 2018
											// XM8M privacy policy
#define MENU_RA_HARDCORE_CONFIRM        2019
											// active Hardcore exit confirmation screen
#define MENU_RA_LOGOUT_CONFIRM          2020
											// pending unlock deletion confirmation screen
#define MENU_RA_RESET_YES                2021
											// confirm reset-requiring RA mode change
#define MENU_RA_RESET_NO                 2022
											// cancel reset-requiring RA mode change
#define MENU_RA_RESET_CONFIRM            2023
											// reset-requiring RA mode confirmation screen
#define MENU_RA_LIBRARY_ITEM_MIN        2100
										// RetroAchievements library rows
#define MENU_RA_ACHIEVEMENT_ITEM_MIN    4000
										// RetroAchievements achievement rows
#define MENU_RA_LEADERBOARD_ITEM_MIN    6000
										// RetroAchievements leaderboard rows
#define MENU_RA_DETAIL_LINE_MIN         8000
										// RetroAchievements detail scroll rows
#define MENU_RA_MAX                     9999
										// maximum

//
// file menu
//
#define MENU_FILE_MIN                   10000
										// minimum
#define MENU_PLAYLIST_ENTRY_MIN          11000
#define MENU_PLAYLIST_ENTRY_MAX          11999

#endif // MENUID_H

#endif // SDL
