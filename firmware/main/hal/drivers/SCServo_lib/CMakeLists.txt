idf_component_register(
    SRCS 
        "src/SCS.cpp"
        "src/SCSCL.cpp"
        "src/SCSerial.cpp"
    INCLUDE_DIRS 
        "src"
    REQUIRES 
        driver
        esp_timer
)

