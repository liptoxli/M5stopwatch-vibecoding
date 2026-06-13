
list(APPEND BMI270_SensorAPI_SRCS
    src/BMI270_SensorAPI/bmi2_ois.c
    src/BMI270_SensorAPI/bmi2.c
    src/BMI270_SensorAPI/bmi270_context.c
    src/BMI270_SensorAPI/bmi270_dsd.c
    src/BMI270_SensorAPI/bmi270_legacy.c
    src/BMI270_SensorAPI/bmi270_maximum_fifo.c
    src/BMI270_SensorAPI/bmi270.c
)

list(APPEND BMM150_SensorAPI_SRCS
    src/BMM150_SensorAPI/bmm150.c
)

idf_component_register(
    SRCS
        "src/bmi270_bmm150.c"
        ${BMI270_SensorAPI_SRCS}
        ${BMM150_SensorAPI_SRCS}
    INCLUDE_DIRS
        "include"
        "src/BMI270_SensorAPI"
        "src/BMM150_SensorAPI"
    REQUIRES
        i2c_bus
        esp_timer
)
