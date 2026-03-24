idf_component_register(
    SRCS "main.c"
    EMBED_TXTFILES "../2.txt"
                       
    INCLUDE_DIRS "."
                           
    REQUIRES driver
)
