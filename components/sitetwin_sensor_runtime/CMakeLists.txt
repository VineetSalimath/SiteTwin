idf_component_register(SRCS
                       "src/board_port_manager.c"
                       "src/final_pcb_board.c"
                       "src/hotswap_scan_scheduler.c"
                       "src/hotswap_zigbee_slot.c"
                       "src/logical_channel_adapter.c"
                       "src/module_instance.c"
                       "src/shared_alarm_indicator.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sitetwin_core)
