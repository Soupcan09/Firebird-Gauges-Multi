idf_component_register(
                        SRCS  "main.c" 
                              "EXIO/TCA9554PWR.c"
                              "LCD_Driver/ST7701S.c" 
                              "Touch_Driver/CST820.c"
                              "Touch_Driver/esp_lcd_touch/esp_lcd_touch.c" 
                              "LVGL_Driver/LVGL_Driver.c"
                              "I2C_Driver/I2C_Driver.c"
                              "PCF85063/PCF85063.c"
                              "QMI8658/QMI8658.c"
                              "SD_Card/SD_MMC.c"
                              "LVGL_UI/LVGL_Example.c"
                              "LVGL_UI/gauge_face_classic.c"
                              "LVGL_UI/needle_img.c"
                              "LVGL_UI/display_gauge.c"
                              "LVGL_UI/splash_img.c"
                              "LVGL_UI/splash_screen.c"
                              "LVGL_UI/settings_screen.c"
                              "LVGL_UI/gauge_screen.c"
                              "LVGL_UI/oil_gauge.c"
                              "LVGL_UI/speed_gauge.c"
                              "Wireless/Wireless.c"
                              "Buzzer/Buzzer.c"
                              "BAT_Driver/BAT_Driver.c"
                              "Temp_Sender/Temp_Sender.c"
                              "Settings/Settings.c"
                              "GPS/GPS.c"
                              "OTA/OTA.c"
                              "OTA/wifi_sta.c"

                        INCLUDE_DIRS "./EXIO"
                                     "./LCD_Driver"
                                     "./Touch_Driver" 
                                     "./Touch_Driver/esp_lcd_touch"  
                                     "./LVGL_Driver"
                                     "./I2C_Driver"
                                     "./PCF85063"
                                     "./QMI8658"
                                     "./SD_Card"
                                     "./LVGL_UI"
                                     "./Wireless"
                                     "./Buzzer"
                                     "./BAT_Driver"
                                     "./Temp_Sender"
                                     "./Settings"
                                     "./GPS"
                                     "./OTA"
                                     "."

                        # ESP-IDF components used by main/.
                        # IMPORTANT: as soon as we add ANY REQUIRES,
                        # ESP-IDF stops auto-discovering components --
                        # so we have to enumerate everything the existing
                        # drivers were getting implicitly, PLUS the new
                        # OTA stuff.  Adding a component here that's
                        # already present is harmless; missing one
                        # causes "fatal error: driver/foo.h: No such
                        # file or directory".
                        REQUIRES
                            # Pre-existing peripheral drivers
                            driver
                            esp_driver_gpio
                            esp_driver_spi
                            esp_driver_i2c
                            esp_driver_uart
                            esp_driver_ledc
                            esp_lcd
                            esp_adc
                            sdmmc
                            fatfs
                            vfs
                            nvs_flash
                            spi_flash
                            # WiFi + Bluetooth (Wireless.c)
                            esp_wifi
                            esp_netif
                            esp_event
                            bt
                            # OTA path additions
                            esp_https_ota
                            esp_http_client
                            app_update
                        )
