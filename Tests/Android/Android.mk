XM8_TEST_PATH := $(call my-dir)
XM8_ROOT := $(abspath $(XM8_TEST_PATH)/../..)
include $(XM8_ROOT)/Builder/Android/app/jni/SDL/Android.mk

# Test-only executables use the same production sources as desktop CTest.
# They do not link the app entry point or launch an Activity.
LOCAL_PATH := $(XM8_TEST_PATH)
XM8_TEST_INCLUDES := $(XM8_ROOT)/Source/UI $(XM8_ROOT)/Source/ePC-8801MA \
 $(XM8_ROOT)/Source/ePC-8801MA/vm $(XM8_ROOT)/Source/ePC-8801MA/vm/pc8801 \
 $(XM8_ROOT)/Source/ePC-8801MA/vm/fmgen $(XM8_ROOT)/Source/RA \
 $(XM8_ROOT)/Builder/Android/app/jni/src

include $(CLEAR_VARS)
LOCAL_MODULE := fileio_error_test
LOCAL_SRC_FILES := ../fileio_error_test.cpp ../../Source/UI/fileio.cpp host.cpp
LOCAL_C_INCLUDES := $(XM8_TEST_INCLUDES)
LOCAL_CPPFLAGS := -std=c++17 -DSDL -D_PC8801MA
LOCAL_CPP_FEATURES := exceptions
LOCAL_SHARED_LIBRARIES := SDL2
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := setting_persistence_test
LOCAL_SRC_FILES := ../setting_persistence_test.cpp ../../Source/UI/setting.cpp ../../Source/UI/fileio.cpp host.cpp
LOCAL_C_INCLUDES := $(XM8_TEST_INCLUDES)
LOCAL_CPPFLAGS := -std=c++17 -DSDL -D_PC8801MA
LOCAL_CPP_FEATURES := exceptions
LOCAL_SHARED_LIBRARIES := SDL2
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := input_test
LOCAL_SRC_FILES := ../input_test.cpp ../../Source/UI/input.cpp ../../Source/UI/softkey.cpp \
 ../../Source/UI/setting.cpp ../../Source/UI/fileio.cpp host.cpp
LOCAL_C_INCLUDES := $(XM8_TEST_INCLUDES)
LOCAL_CPPFLAGS := -std=c++17 -DSDL -D_PC8801MA
LOCAL_CPP_FEATURES := exceptions
LOCAL_SHARED_LIBRARIES := SDL2
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := audio_test
LOCAL_SRC_FILES := ../audio_test.cpp host.cpp
LOCAL_C_INCLUDES := $(XM8_TEST_INCLUDES)
LOCAL_CPPFLAGS := -std=c++17 -DSDL -D_PC8801MA
LOCAL_CPP_FEATURES := exceptions
LOCAL_SHARED_LIBRARIES := SDL2
include $(BUILD_EXECUTABLE)
