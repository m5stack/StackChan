cmake_minimum_required(VERSION 3.16)

project(stackchan_firmware_tests LANGUAGES CXX)

enable_testing()

add_executable(servo_math_test
    servo_math_test.cpp
    ../main/stackchan/motion/servo_math.cpp
)

target_include_directories(servo_math_test PRIVATE
    ../main
)

target_compile_features(servo_math_test PRIVATE cxx_std_17)

add_test(NAME servo_math_test COMMAND $<TARGET_FILE:servo_math_test>)
