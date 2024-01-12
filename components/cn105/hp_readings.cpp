#include "cn105.h"

/**
 * Seek the byte pointer to the beginning of the array
 * Initializes few variables
*/
void CN105Climate::initBytePointer() {
    this->foundStart = false;
    this->bytesRead = 0;
    this->dataLength = -1;
    this->command = 0;
}


//#region input_parsing

/**
 *
 * La taille totale d'une trame, se compose de plusieurs éléments :
 * Taille du Header : Le header a une longueur fixe de 5 octets (INFOHEADER_LEN).
 * Longueur des Données : La longueur des données est variable et est spécifiée par le quatrième octet du header (header[4]).
 * Checksum : Il y a 1 octet de checksum à la fin de la trame.
 *
 * La taille totale d'une trame est donc la somme de ces éléments : taille du header (5 octets) + longueur des données (variable) + checksum (1 octet).
 * Pour calculer la taille totale, on peut utiliser la formule :
 * Taille totale = 5 (header) + Longueur des données + 1 (checksum)
 * La taille totale dépend de la longueur spécifique des données pour chaque trame individuelle.
 */
void CN105Climate::parse(byte inputData) {

    ESP_LOGV("Decoder", "--> %02X [nb: %d]", inputData, this->bytesRead);

    if (!this->foundStart) {                // no packet yet
        if (inputData == HEADER[0]) {
            this->foundStart = true;
            storedInputData[this->bytesRead++] = inputData;
        } else {
            // unknown bytes
        }
    } else {                                // we are getting a packet
        storedInputData[this->bytesRead] = inputData;

        checkHeader(inputData);

        if (this->dataLength != -1) {       // is header complete ?

            if ((this->bytesRead) == this->dataLength + 5) {

                this->processDataPacket();
                this->initBytePointer();
            } else {                                        // packet is still filling
                this->bytesRead++;                          // more data to come
            }
        } else {
            ESP_LOGV("Decoder", "data length toujours pas connu");
            // header is not complete yet
            this->bytesRead++;
        }
    }

}


bool CN105Climate::checkSum() {
    // TODO: use the CN105Climate::checkSum(byte bytes[], int len) function

    byte packetCheckSum = storedInputData[this->bytesRead];
    byte processedCS = 0;

    ESP_LOGV("chkSum", "controling chkSum should be: %02X ", packetCheckSum);

    for (int i = 0;i < this->dataLength + 5;i++) {
        ESP_LOGV("chkSum", "adding %02X to %02X --> ", this->storedInputData[i], processedCS, processedCS + this->storedInputData[i]);
        processedCS += this->storedInputData[i];
    }

    processedCS = (0xfc - processedCS) & 0xff;

    if (packetCheckSum == processedCS) {
        ESP_LOGD("chkSum", "OK-> %02X=%02X ", processedCS, packetCheckSum);
    } else {
        ESP_LOGW("chkSum", "KO-> %02X!=%02X ", processedCS, packetCheckSum);
    }

    return (packetCheckSum == processedCS);
}


void CN105Climate::checkHeader(byte inputData) {
    if (this->bytesRead == 4) {
        if (storedInputData[2] == HEADER[2] && storedInputData[3] == HEADER[3]) {
            ESP_LOGV("Header", "header matches HEADER");
            ESP_LOGV("Header", "[%02X] (%02X) %02X %02X [%02X]<-- header", storedInputData[0], storedInputData[1], storedInputData[2], storedInputData[3], storedInputData[4]);
            ESP_LOGD("Header", "command: (%02X) data length: [%02X]<-- header", storedInputData[1], storedInputData[4]);
            this->command = storedInputData[1];
        }
        this->dataLength = storedInputData[4];
    }
}

void CN105Climate::processInput(void) {

    /*if (this->get_hw_serial_()->available()) {
        this->isReading = true;
    }*/

    while (this->get_hw_serial_()->available()) {
        int inputData = this->get_hw_serial_()->read();
        parse(inputData);
    }
    //this->isReading = false;
}

void CN105Climate::processDataPacket() {

    ESP_LOGV(TAG, "processing data packet...");

    this->data = &this->storedInputData[5];

    this->hpPacketDebug(this->storedInputData, this->bytesRead + 1, "READ");

    if (this->checkSum()) {
        // checkPoint of a heatpump response
        this->lastResponseMs = CUSTOM_MILLIS;    //esphome::CUSTOM_MILLIS;        

        // processing the specific command
        processCommand();
    }
}
void CN105Climate::getDataFromResponsePacket() {
    switch (this->data[0]) {
    case 0x02: {            /* setting information */
        ESP_LOGD("Decoder", "[0x02 is settings]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x02: Data -> Settings");
        heatpumpSettings receivedSettings;
        receivedSettings.connected = true;      // we're here so we're connected (actually not used property)
        receivedSettings.power = lookupByteMapValue(POWER_MAP, POWER, 2, data[3]);
        receivedSettings.iSee = data[4] > 0x08 ? true : false;
        receivedSettings.mode = lookupByteMapValue(MODE_MAP, MODE, 5, receivedSettings.iSee ? (data[4] - 0x08) : data[4]);

        ESP_LOGD("Decoder", "[Power : %s]", receivedSettings.power);
        ESP_LOGD("Decoder", "[iSee  : %d]", receivedSettings.iSee);
        ESP_LOGD("Decoder", "[Mode  : %s]", receivedSettings.mode);

        if (data[11] != 0x00) {
            int temp = data[11];
            temp -= 128;
            receivedSettings.temperature = (float)temp / 2;
            tempMode = true;
        } else {
            receivedSettings.temperature = lookupByteMapValue(TEMP_MAP, TEMP, 16, data[5]);
            ESP_LOGD("Decoder", "[Consigne °C: %f]", receivedSettings.temperature);
        }

        receivedSettings.fan = lookupByteMapValue(FAN_MAP, FAN, 6, data[6]);
        ESP_LOGD("Decoder", "[Fan: %s]", receivedSettings.fan);

        receivedSettings.vane = lookupByteMapValue(VANE_MAP, VANE, 7, data[7]);
        ESP_LOGD("Decoder", "[Vane: %s]", receivedSettings.vane);


        receivedSettings.wideVane = lookupByteMapValue(WIDEVANE_MAP, WIDEVANE, 7, data[10] & 0x0F);



        wideVaneAdj = (data[10] & 0xF0) == 0x80 ? true : false;

        ESP_LOGD("Decoder", "[wideVane: %s (adj:%d)]", receivedSettings.wideVane, wideVaneAdj);

        // moved to settingsChanged()
        //currentSettings = receivedSettings;

        if (this->firstRun) {
            //wantedSettings = currentSettings;
            wantedSettings = receivedSettings;
            firstRun = false;
        }
        this->iSee_sensor->publish_state(receivedSettings.iSee);

        this->settingsChanged(receivedSettings);

    }


             break;
    case 0x03: {
        /* room temperature reading */
        ESP_LOGD("Decoder", "[0x03 room temperature]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x03: Data -> Room temperature");
        heatpumpStatus receivedStatus;

        if (data[6] != 0x00) {
            int temp = data[6];
            temp -= 128;
            receivedStatus.roomTemperature = (float)temp / 2;
        } else {
            receivedStatus.roomTemperature = lookupByteMapValue(ROOM_TEMP_MAP, ROOM_TEMP, 32, data[3]);
        }
        ESP_LOGD("Decoder", "[Room °C: %f]", receivedStatus.roomTemperature);

        currentStatus.roomTemperature = receivedStatus.roomTemperature;
        this->current_temperature = currentStatus.roomTemperature;

    }
             break;

    case 0x04:
        /* unknown */
        ESP_LOGI("Decoder", "[0x04 is unknown]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x04: Data -> Unknown");
        break;

    case 0x05:
        /* timer packet */
        ESP_LOGW("Decoder", "[0x05 is timer packet: not implemented]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x05: Data -> Timer Packet");
        break;

    case 0x06: {
        /* status */
        ESP_LOGD("Decoder", "[0x06 is status]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x06: Data -> Heatpump Status");

        // reset counter (because a reply indicates it is connected)
        this->nonResponseCounter = 0;

        heatpumpStatus receivedStatus;
        receivedStatus.operating = data[4];
        receivedStatus.compressorFrequency = data[3];
        currentStatus.operating = receivedStatus.operating;
        currentStatus.compressorFrequency = receivedStatus.compressorFrequency;

        this->compressor_frequency_sensor->publish_state(currentStatus.compressorFrequency);

        ESP_LOGD("Decoder", "[Operating: %d]", currentStatus.operating);
        ESP_LOGD("Decoder", "[Compressor Freq: %d]", currentStatus.compressorFrequency);

        // RCVD_PKT_STATUS;
    }
             break;

    case 0x09:
        /* unknown */
        ESP_LOGD("Decoder", "[0x09 is unknown]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x09: Data -> Unknown");
        break;
    case 0x20:
    case 0x22: {
        ESP_LOGD("Decoder", "[Packet Functions 0x20 et 0x22]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x20/0x22: Data -> Packet functions");
        if (dataLength == 0x10) {
            if (data[0] == 0x20) {
                functions.setData1(&data[1]);
            } else {
                functions.setData2(&data[1]);
            }

            // RCVD_PKT_FUNCTIONS;
        }

    }
             break;

    default:
        ESP_LOGW("Decoder", "type de packet [%02X] <-- inconnu et inattendu", data[0]);
        //this->last_received_packet_sensor->publish_state("0x62-> ?? : Data -> Unknown");
        break;
    }
}
void CN105Climate::processCommand() {
    switch (this->command) {
    case 0x61:  /* last update was successful */
        ESP_LOGI(TAG, "Last heatpump data update successfull!");

        //this->last_received_packet_sensor->publish_state("0x61: update success");

        if (!this->autoUpdate) {
            this->buildAndSendRequestsInfoPackets();
        }

        break;

    case 0x62:  /* packet contains data (room °C, settings, timer, status, or functions...)*/
        this->getDataFromResponsePacket();

        break;
    case 0x7a:
        ESP_LOGI(TAG, "--> Heatpump did reply: connection success! <--");
        this->isHeatpumpConnected_ = true;
        //this->last_received_packet_sensor->publish_state("0x7A: Connection success");

        programUpdateInterval();        // we know a check in this method is done on autoupdate value        
        break;
    default:
        break;
    }
}


void CN105Climate::settingsChanged(heatpumpSettings settings) {

    /*if (settings.power == NULL) {
        // should never happen because settingsChanged is only called from getDataFromResponsePacket()
        ESP_LOGW(TAG, "Waiting for HeatPump to read the settings the first time.");
        return;
    }*/


    checkPowerAndModeSettings(settings);

    this->updateAction();

    checkFanSettings(settings);

    checkVaneSettings(settings);

    /*
     * ******** HANDLE TARGET TEMPERATURE CHANGES ********
     */

    this->currentSettings.temperature = settings.temperature;
    this->currentSettings.iSee = settings.iSee;
    this->currentSettings.connected = settings.connected;

    this->target_temperature = currentSettings.temperature;
    ESP_LOGD(TAG, "Target temp is: %f", this->target_temperature);

    /*
     * ******** Publish state back to ESPHome. ********
     */
    this->publish_state();

}
void CN105Climate::checkVaneSettings(heatpumpSettings& settings) {
    /* ******** HANDLE MITSUBISHI VANE CHANGES ********
         * const char* VANE_MAP[7]        = {"AUTO", "1", "2", "3", "4", "5", "SWING"};
         */
    if (this->hasChanged(currentSettings.vane, settings.vane, "vane")) { // vane setting change ?
        ESP_LOGI(TAG, "vane setting changed");
        currentSettings.vane = settings.vane;

        if (strcmp(currentSettings.vane, "SWING") == 0) {
            this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
        } else {
            this->swing_mode = climate::CLIMATE_SWING_OFF;
        }
        ESP_LOGD(TAG, "Swing mode is: %i", this->swing_mode);
    }

    if (this->hasChanged(this->van_orientation->state.c_str(), settings.vane, "select vane")) {
        ESP_LOGI(TAG, "vane setting (extra select component) changed");
        this->van_orientation->publish_state(currentSettings.vane);
    }
}
void CN105Climate::checkFanSettings(heatpumpSettings& settings) {
    /*
         * ******* HANDLE FAN CHANGES ********
         *
         * const char* FAN_MAP[6]         = {"AUTO", "QUIET", "1", "2", "3", "4"};
         */
         // currentSettings.fan== NULL is true when it is the first time we get en answer from hp

    if (this->hasChanged(currentSettings.fan, settings.fan, "fan")) { // fan setting change ?
        ESP_LOGI(TAG, "fan setting changed");
        currentSettings.fan = settings.fan;
        if (strcmp(currentSettings.fan, "QUIET") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_QUIET;
        } else if (strcmp(currentSettings.fan, "1") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_LOW;
        } else if (strcmp(currentSettings.fan, "2") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        } else if (strcmp(currentSettings.fan, "3") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_MIDDLE;
        } else if (strcmp(currentSettings.fan, "4") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_HIGH;
        } else { //case "AUTO" or default:
            this->fan_mode = climate::CLIMATE_FAN_AUTO;
        }
        ESP_LOGD(TAG, "Fan mode is: %i", this->fan_mode);
    }
}
void CN105Climate::checkPowerAndModeSettings(heatpumpSettings& settings) {
    // currentSettings.power== NULL is true when it is the first time we get en answer from hp
    if (this->hasChanged(currentSettings.power, settings.power, "power") ||
        this->hasChanged(currentSettings.mode, settings.mode, "mode")) {           // mode or power change ?

        ESP_LOGI(TAG, "power or mode changed");
        currentSettings.power = settings.power;
        currentSettings.mode = settings.mode;

        if (strcmp(currentSettings.power, "ON") == 0) {
            if (strcmp(currentSettings.mode, "HEAT") == 0) {
                this->mode = climate::CLIMATE_MODE_HEAT;
            } else if (strcmp(currentSettings.mode, "DRY") == 0) {
                this->mode = climate::CLIMATE_MODE_DRY;
            } else if (strcmp(currentSettings.mode, "COOL") == 0) {
                this->mode = climate::CLIMATE_MODE_COOL;
                /*if (cool_setpoint != currentSettings.temperature) {
                    cool_setpoint = currentSettings.temperature;
                    save(currentSettings.temperature, cool_storage);
                }*/
            } else if (strcmp(currentSettings.mode, "FAN") == 0) {
                this->mode = climate::CLIMATE_MODE_FAN_ONLY;
            } else if (strcmp(currentSettings.mode, "AUTO") == 0) {
                this->mode = climate::CLIMATE_MODE_HEAT_COOL;
            } else {
                ESP_LOGW(
                    TAG,
                    "Unknown climate mode value %s received from HeatPump",
                    currentSettings.mode
                );
            }
        } else {
            this->mode = climate::CLIMATE_MODE_OFF;
        }
    }
}

//#endregion input_parsing