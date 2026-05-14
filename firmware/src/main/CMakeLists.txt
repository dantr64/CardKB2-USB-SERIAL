set(src_dirs        
    .
    ./app
    ./bsp
)

set(include_dirs      
    .
    ./app
    ./bsp
) 

idf_component_register(
    SRC_DIRS 
        ${src_dirs} 
    INCLUDE_DIRS 
        ${include_dirs}
    REQUIRES 
        esp_hid
        esp_driver_ledc
        esp_driver_i2c
        esp_driver_uart
        esp_wifi
        esp_netif
        bt
    PRIV_REQUIRES 
        nvs_flash
)

