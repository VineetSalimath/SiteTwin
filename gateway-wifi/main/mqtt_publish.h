#ifndef MQTT_PUBLISH_H
#define MQTT_PUBLISH_H

int gw_mqtt_publish(const char *topic, const char *payload);
int gw_mqtt_is_connected(void);

#endif