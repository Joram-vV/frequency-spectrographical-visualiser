idf_component_register(
    SRCS "main.c" "pid_controller.c"
    EMBED_TXTFILES "../2.txt"
                       
    INCLUDE_DIRS "../components"
                           
    REQUIRES 
        driver
        esp_timer
        vl6180
)
