idf_component_register(SRCS "analog.c" "rfid.c" "encoders.c" "me_slot_config.c" "buttons.c" "executor.c" "3n_mosfet.c" "encoders.c" "rfid.c" "lidars.c" "tachometer.c" "analog.c"
                    INCLUDE_DIRS "include"
                    REQUIRES stateConfig reporter audioPlayer reporter pn532 esp32-rotary-encoder)

 