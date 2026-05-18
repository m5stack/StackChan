idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_spi esp_driver_uart esp_lcd esp_timer esp_video human_face_detect
                       EMBED_FILES "test_face_320x240.rgb565")
