set(srcs "main.c"
         "gatt_svr.c"
         "custom_gatt_svc.c")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS ".")
