# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c         # list the source files of this component
    INCLUDE_DIRS "."       # optional, add here public include directories
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES nvs_flash bt esp_http_client esp_https_ota app_update esp_partition	# optional, list the public requirements (component names)
    PRIV_REQUIRES esp_wifi esp_netif led_strip      # optional, list the private requirements
)

target_add_binary_data(${COMPONENT_TARGET} "server.crt" TEXT)