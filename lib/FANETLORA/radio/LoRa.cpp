// Copyright (c) Sandeep Mistry. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "LoRa.h"

// registers
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_OCP                  0x0b
#define REG_LNA                  0x0c
#define REG_FIFO_ADDR_PTR        0x0d
#define REG_FIFO_TX_BASE_ADDR    0x0e
#define REG_FIFO_RX_BASE_ADDR    0x0f
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_PKT_SNR_VALUE        0x19
#define REG_PKT_RSSI_VALUE       0x1a
#define REG_MODEM_CONFIG_1       0x1d
#define REG_MODEM_CONFIG_2       0x1e
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MODEM_CONFIG_3       0x26
#define REG_FREQ_ERROR_MSB       0x28
#define REG_FREQ_ERROR_MID       0x29
#define REG_FREQ_ERROR_LSB       0x2a
#define REG_RSSI_WIDEBAND        0x2c
#define REG_DETECTION_OPTIMIZE   0x31
#define REG_INVERTIQ             0x33
#define REG_DETECTION_THRESHOLD  0x37
#define REG_SYNC_WORD            0x39
#define REG_INVERTIQ2            0x3b
#define REG_DIO_MAPPING_1        0x40
#define REG_VERSION              0x42
#define REG_PA_DAC               0x4d

// modes
//#define MODE_LONG_RANGE_MODE     0x80
#define MODE_SLEEP               0x00
#define MODE_STDBY               0x01
#define MODE_TX                  0x03
#define MODE_RX_CONTINUOUS       0x05
#define MODE_RX_SINGLE           0x06

// PA config
#define PA_BOOST                 0x80

// IRQ masks
#define IRQ_TX_DONE_MASK           0x08
#define IRQ_PAYLOAD_CRC_ERROR_MASK 0x20
#define IRQ_RX_DONE_MASK           0x40

#define MAX_PKT_LENGTH           255

#if (ESP8266 || ESP32)
    #define ISR_PREFIX ICACHE_RAM_ATTR
#else
    #define ISR_PREFIX
#endif

float sx_airtime = 0.0f;
bool armed = false;
bool _FskMode = false;



LoRaClass::LoRaClass() :
  _spiSettings(LORA_DEFAULT_SPI_FREQUENCY, MSBFIRST, SPI_MODE0),
  _spi(&LORA_DEFAULT_SPI),
  _ss(LORA_DEFAULT_SS_PIN), _reset(LORA_DEFAULT_RESET_PIN), _dio0(LORA_DEFAULT_DIO0_PIN),
  _frequency(0),
  _packetIndex(0),
  _implicitHeaderMode(0),
  _onReceive(NULL)
{
  // overide Stream timeout value
  setTimeout(0);
}
uint8_t MODE_LONG_RANGE_MODE     =0x80;


int LoRaClass::begin(long frequency)
{
#if defined(ARDUINO_SAMD_MKRWAN1300) || defined(ARDUINO_SAMD_MKRWAN1310)
  pinMode(LORA_IRQ_DUMB, OUTPUT);
  digitalWrite(LORA_IRQ_DUMB, LOW);

  // Hardware reset
  pinMode(LORA_BOOT0, OUTPUT);
  digitalWrite(LORA_BOOT0, LOW);

  digitalWrite(LORA_RESET, HIGH);
  delay(200);
  digitalWrite(LORA_RESET, LOW);
  delay(200);
  digitalWrite(LORA_RESET, HIGH);
  delay(50);
#endif

  if(_FskMode)
    MODE_LONG_RANGE_MODE     =0x00;
  else 
    MODE_LONG_RANGE_MODE     =0x80;
    
  // setup pins
  pinMode(_ss, OUTPUT);
  // set SS high
  digitalWrite(_ss, HIGH);

  if (_reset != -1) {
    pinMode(_reset, OUTPUT);

    // perform reset
    digitalWrite(_reset, LOW);
    delay(10);
    digitalWrite(_reset, HIGH);
    delay(10);
  }

  // start SPI
  _spi->begin();

  // check version
  uint8_t version = readRegister(REG_VERSION);
  if (version != 0x12) {
    return 0;
  }

  // put in sleep mode
  sleep();

  // set frequency
  setFrequency(frequency);

  // set base addresses
  writeRegister(REG_FIFO_TX_BASE_ADDR, 0);
  writeRegister(REG_FIFO_RX_BASE_ADDR, 0);

  // set LNA boost
  writeRegister(REG_LNA, readRegister(REG_LNA) | 0x03);

  // set auto AGC
  writeRegister(REG_MODEM_CONFIG_3, 0x04);

  // set output power to 17 dBm
  setTxPower(17);

  // put in standby mode
  idle();

  return 1;
}

uint8_t LoRaClass::getOpMode(void)
{
	return readRegister(REG_OP_MODE);
}

bool LoRaClass::setArmed(bool mode,void(*callback)(int))
{
	if(mode == armed)
		return true;

	/* transmit packet in buffer */
	while(getOpMode() == (MODE_LONG_RANGE_MODE|LORA_TX_MODE))
		delay(1);

	LoRa.onReceive(NULL);

	/* store mode */
	uint8_t opmode = getOpMode();

	/* update state */
	armed = mode;

	if(mode)
	{
		/* enable rx */
		if(opmode != ( MODE_LONG_RANGE_MODE|LORA_TX_MODE))
			LoRa.receive();
	}
	else
	{
		/* enter power save */
		LoRa.sleep();
	}

  //LoRa.onReceive(callback); // we don't use the interrupt --> crashes ESP
	return true;
}

bool LoRaClass::isArmed(void)
{
	return armed;
}

void LoRaClass::end()
{
  // put in sleep mode
  sleep();

  // stop SPI
  _spi->end();
}

int LoRaClass::beginPacket(int implicitHeader)
{
  if (isTransmitting()) {
    return 0;
  }

  // put in standby mode
  idle();

  if (implicitHeader) {
    implicitHeaderMode();
  } else {
    explicitHeaderMode();
  }

  // reset FIFO address and paload length
  writeRegister(REG_FIFO_ADDR_PTR, 0);
  if (_FskMode)
	  writeRegister(REG_PAYLOADLENGTH, 0);
  else
	  writeRegister(REG_PAYLOAD_LENGTH, 0);

  return 1;
}

int LoRaClass::endPacket(bool async)
{
  // put in TX mode
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

  if (async) {
    // grace time is required for the radio
    delayMicroseconds(150);
  } else {
    // wait for TX done
    while ((readRegister(REG_IRQ_FLAGS) & IRQ_TX_DONE_MASK) == 0) {
      yield();
    }
    // clear IRQ's
    writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);
  }

  return 1;
}

bool LoRaClass::isTransmitting()
{
  if ((readRegister(REG_OP_MODE) & MODE_TX) == MODE_TX) {
    return true;
  }

  if (readRegister(REG_IRQ_FLAGS) & IRQ_TX_DONE_MASK) {
    // clear IRQ's
    writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);
  }

  return false;
}

int LoRaClass::parsePacket(int size)
{
  int packetLength = 0;
  int irqFlags = readRegister(REG_IRQ_FLAGS);

  if (size > 0) {
    implicitHeaderMode();

	if (_FskMode)
		writeRegister(REG_PAYLOADLENGTH,  size & 0xff);
	else
		writeRegister(REG_PAYLOAD_LENGTH, size & 0xff);
  } else {
    explicitHeaderMode();
  }

  // clear IRQ's
  writeRegister(REG_IRQ_FLAGS, irqFlags);

  if ((irqFlags & IRQ_RX_DONE_MASK) && (irqFlags & IRQ_PAYLOAD_CRC_ERROR_MASK) == 0) {
    // received a packet
    _packetIndex = 0;

    // read packet length
    if (_implicitHeaderMode) {
		if (_FskMode)
			packetLength = readRegister(REG_PAYLOADLENGTH);
		else
			packetLength = readRegister(REG_PAYLOAD_LENGTH);
    } else {
      packetLength = readRegister(REG_RX_NB_BYTES);
    }

    // set FIFO address to current RX address
    writeRegister(REG_FIFO_ADDR_PTR, readRegister(REG_FIFO_RX_CURRENT_ADDR));

    // put in standby mode
    idle();
  } else if (readRegister(REG_OP_MODE) != (MODE_LONG_RANGE_MODE | MODE_RX_SINGLE)) {
    // not currently in RX mode

    // reset FIFO address
    writeRegister(REG_FIFO_ADDR_PTR, 0);

    // put in single RX mode
    writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_SINGLE);
  }

  return packetLength;
}

bool LoRaClass::setOpMode(uint8_t mode)
{
#if (SX1276_debug_mode > 0)
	switch (mode)
	{
	case MODE_LONG_RANGE_MODE|LORA_RXCONT_MODE:
		Serial.print("## SX1276 opmode: rx continous\n");
	break;
	case MODE_LONG_RANGE_MODE|LORA_TX_MODE:
		Serial.print("## SX1276 opmode: tx\n");
	break;
	case MODE_LONG_RANGE_MODE| LORA_SLEEP_MODE:
		Serial.print("## SX1276 opmode: sleep\n");
	break;
	case MODE_LONG_RANGE_MODE|LORA_STANDBY_MODE:
		Serial.print("## SX1276 opmode: standby\n");
	break;
	case LORA_CAD_MODE:
		Serial.print("## SX1276 opmode: cad\n");
	break;
	default:
		Serial.print("## SX1276 opmode: unknown\n");
	}
#endif

	/* set mode */
	writeRegister(REG_OP_MODE, mode);

	/* wait for frequency synthesis, 10ms timeout */
	uint8_t opmode = 0;
	for(int i=0; i<10 && opmode != mode; i++)
	{
		delay(1);
		opmode = readRegister(REG_OP_MODE);
	}

//#if (SX1276_debug_mode > 1)
//	Serial.printf("## SX1276 opmode: %02X\n", opmode);
//#endif
#if (SX1276_debug_mode > 0)
	switch (mode)
	{
	case MODE_LONG_RANGE_MODE|LORA_RXCONT_MODE:
		Serial.print("## SX1276 opmode: rx continous\n");
	break;
	case MODE_LONG_RANGE_MODE|LORA_TX_MODE:
		Serial.print("## SX1276 opmode: tx\n");
	break;
	case MODE_LONG_RANGE_MODE|LORA_SLEEP_MODE:
		Serial.print("## SX1276 opmode: sleep\n");
	break;
	case MODE_LONG_RANGE_MODE|LORA_STANDBY_MODE:
		Serial.print("## SX1276 opmode: standby\n");
	break;
	case LORA_CAD_MODE:
		Serial.print("## SX1276 opmode: cad\n");
	break;
	default:
		Serial.print("## SX1276 opmode: unknown\n");
	}
#endif


	return mode == opmode;
}


int LoRaClass::channel_free4tx(bool doCAD)
{
	uint8_t mode = getOpMode();
	mode &= LORA_MODE_MASK;

	/* are we transmitting anyway? */
	if(mode == (MODE_LONG_RANGE_MODE|LORA_TX_MODE))
		return TX_TX_ONGOING;

	/* in case of receiving, is it ongoing? */
	for(int i=0; i<4 && (mode == (MODE_LONG_RANGE_MODE|LORA_RXCONT_MODE) || mode == (MODE_LONG_RANGE_MODE|LORA_RXSINGLE_MODE)); i++)
	{
		if(readRegister(REG_MODEM_STAT) & 0x0B)
			return TX_RX_ONGOING;
		delay(1);
	}

	/* CAD not required */
	if(doCAD == false)
		return TX_OK;

	/*
	 * CAD
	 */

	setOpMode(MODE_LONG_RANGE_MODE|LORA_STANDBY_MODE);
	writeRegister(REG_IRQ_FLAGS, IRQ_CAD_DONE | IRQ_CAD_DETECTED);	/* clearing flags */
	setOpMode(LORA_CAD_MODE);

	/* wait for CAD completion */
//TODO: it may enter a life lock here...
	uint8_t iflags;
	while(((iflags=readRegister(REG_IRQ_FLAGS)) & IRQ_CAD_DONE) == 0)
		delay(1);

	if(iflags & IRQ_CAD_DETECTED)
	{
		/* re-establish old mode */
		if(mode == (MODE_LONG_RANGE_MODE|LORA_RXCONT_MODE) || mode ==(MODE_LONG_RANGE_MODE|LORA_RXSINGLE_MODE)|| mode == (MODE_LONG_RANGE_MODE|LORA_SLEEP_MODE))
			setOpMode(mode);

		return TX_RX_ONGOING;
	}

	return TX_OK;
}

int LoRaClass::writeRegister_burst(uint8_t address, uint8_t *data, int length){
	select();
	/* bit 7 set to write registers */
	address |= 0x80;

  _spi->beginTransaction(_spiSettings);
  _spi->transfer(address);
  _spi->transferBytes(data,0,length);
  _spi->endTransaction();
	unselect();

	return 0;
}

bool LoRaClass::setCodingRate(uint8_t cr)
{
#if (SX1276_debug_mode > 0)
	Serial.printf("## SX1276 CR:%02X\n", cr);
#endif

	/* store state */
	uint8_t opmode = getOpMode();
	if(opmode == (MODE_LONG_RANGE_MODE|LORA_TX_MODE))
		return false;

	/* set appropriate mode */
	if(opmode != (MODE_LONG_RANGE_MODE|LORA_STANDBY_MODE) && !setOpMode((MODE_LONG_RANGE_MODE|LORA_STANDBY_MODE)))
		return false;

	uint8_t config1 = readRegister(REG_MODEM_CONFIG_1);
	config1 = (cr&CR_MASK) | (config1&~CR_MASK);
	writeRegister(REG_MODEM_CONFIG_1, config1);

	/* restore state */
	if(opmode !=(MODE_LONG_RANGE_MODE| LORA_STANDBY_MODE))
		return setOpMode(opmode);
	else
		return true;
}

float LoRaClass::expectedAirTime_ms(void)
{
	/* LORA */
        float bw = 0.0f;
        uint8_t cfg1 = readRegister(REG_MODEM_CONFIG_1);
        uint8_t bw_reg = cfg1 & 0xC0;
        switch( bw_reg )
        {
        case 0:		// 125 kHz
            bw = 125000.0f;
            break;
        case 0x40: 	// 250 kHz
            bw = 250000.0f;
            break;
        case 0x80: 	// 500 kHz
            bw = 500000.0f;
            break;
        }

        // Symbol rate : time for one symbol (secs)
        uint8_t sf_reg = readRegister(REG_MODEM_CONFIG_2)>>4;
        //float sf = sf_reg<6 ? 6.0f : sf_reg;
        float rs = bw / ( 1 << sf_reg );
        float ts = 1 / rs;
        // time of preamble
        float tPreamble = ( readRegister(REG_PREAMBLE_LSB) + 4.25f ) * ts;		//note: assuming preamble < 256
        // Symbol length of payload and time
		int pktlen = 0;
		if (_FskMode)
			pktlen = readRegister(REG_PAYLOADLENGTH);
		else
			pktlen = readRegister(REG_PAYLOAD_LENGTH);					//note: assuming CRC on -> 16, fixed length
			
        int coderate = (cfg1 >> 3) & 0x7;
        float tmp = ceil( (8 * pktlen - 4 * sf_reg + 28 + 16 - 20) / (float)( 4 * ( sf_reg ) ) ) * ( coderate + 4 );
        float nPayload = 8 + ( ( tmp > 0 ) ? tmp : 0 );
        float tPayload = nPayload * ts;
        // Time on air
        float tOnAir = (tPreamble + tPayload) * 1000.0f;

        return tOnAir;
}


int LoRaClass::writeFifo(uint8_t addr, uint8_t *data, int length)
{
	/* select location */
	writeRegister(REG_FIFO_ADDR_PTR, addr);

	/* write data */
	return writeRegister_burst(REG_FIFO, data, length);
}

int LoRaClass::sendFrame(uint8_t *data, int length, uint8_t cr){
#if (SX1276_debug_mode > 0)
	Serial.printf("## SX1276 send frame...\n");
#endif

	/* channel accessible? */
	int state = channel_free4tx(true);
	if(state != TX_OK)
		return state;

	/*
	 * Prepare TX
	 */
	setOpMode((MODE_LONG_RANGE_MODE|LORA_STANDBY_MODE));

	//todo: check fifo is empty, no rx data..

	/* adjust coding rate */
	setCodingRate(cr);

	/* upload frame */
  writeFifo(0x00, data, length);
	writeRegister(REG_FIFO_TX_BASE_ADDR, 0x00);
	
	if (_FskMode)
			writeRegister(REG_PAYLOADLENGTH,length);
	else
		writeRegister(REG_PAYLOAD_LENGTH, length);

	/* prepare irq */
	if(_onReceive)
	{
		/* clear flag */
		//writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE);
		//setDio0Irq(DIO0_TX_DONE_LORA);
	}

	/* update air time */
	sx_airtime += expectedAirTime_ms();

	/* tx */
	setOpMode(MODE_LONG_RANGE_MODE|LORA_TX_MODE);
  // clear IRQ's
  writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);

	/* bypass waiting */
	if(_onReceive)
	{
#if (SX1276_debug_mode > 1)
		Serial.printf("INT\n");
#endif
		writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
    return TX_OK;
	}

	for(int i=0; i<100 && getOpMode() == (MODE_LONG_RANGE_MODE|LORA_TX_MODE); i++)
	{
#if (SX1276_debug_mode > 0)
		Serial.printf(".");
#endif
		delay(5);
	}
#if (SX1276_debug_mode > 0)
	Serial.printf("done\n");
#endif
	return TX_OK;

}

int LoRaClass::getFrame(uint8_t *data, int max_length){
	const int received = readRegister(REG_RX_NB_BYTES);
	const int rxstartaddr = readRegister(REG_FIFO_RX_CURRENT_ADDR);
	readFifo(rxstartaddr, data, min(received, max_length));

	return min(received, max_length);
}

void LoRaClass::readFifo(uint8_t addr, uint8_t *data, int length){
	/* select location */
	writeRegister(REG_FIFO_ADDR_PTR, addr);

	/* upload data */
	select();
	_spi->transfer(REG_FIFO);
        for (int i = 0; i < length; i++)
		data[i] = _spi->transfer(0);
	unselect();

#if (SX1276_debug_mode > 1)
	Serial.print(F("## SX1276 read fifo, length="));
	Serial.print(length, DEC);

	Serial.print(" [");
	for (int i = 0; i < length; i++)
	{
		Serial.print(data[i], HEX);
		if(i < length-1)
			Serial.print(", ");
	}
	Serial.println("]");
#endif

}

int LoRaClass::getRssi(void){
  /*
  const int pktsnr = (int8_t) readRegister(REG_PKT_SNR_VALUE);
	int rssi = -139 + readRegister(REG_PKT_RSSI_VALUE);
	if(pktsnr < 0)
		rssi += ((pktsnr-2)/4);			//note: correct rounding for negative numbers
  return rssi;
  */
  return (readRegister(REG_PKT_RSSI_VALUE) - (_frequency < 868E6 ? 164 : 157));
}


int LoRaClass::packetRssi()
{
  return (readRegister(REG_PKT_RSSI_VALUE) - (_frequency < 868E6 ? 164 : 157));
}

float LoRaClass::packetSnr()
{
  return ((int8_t)readRegister(REG_PKT_SNR_VALUE)) * 0.25;
}

long LoRaClass::packetFrequencyError()
{
  int32_t freqError = 0;
  freqError = static_cast<int32_t>(readRegister(REG_FREQ_ERROR_MSB) & B111);
  freqError <<= 8L;
  freqError += static_cast<int32_t>(readRegister(REG_FREQ_ERROR_MID));
  freqError <<= 8L;
  freqError += static_cast<int32_t>(readRegister(REG_FREQ_ERROR_LSB));

  if (readRegister(REG_FREQ_ERROR_MSB) & B1000) { // Sign bit is on
     freqError -= 524288; // B1000'0000'0000'0000'0000
  }

  const float fXtal = 32E6; // FXOSC: crystal oscillator (XTAL) frequency (2.5. Chip Specification, p. 14)
  const float fError = ((static_cast<float>(freqError) * (1L << 24)) / fXtal) * (getSignalBandwidth() / 500000.0f); // p. 37

  return static_cast<long>(fError);
}

size_t LoRaClass::write(uint8_t byte)
{
  return write(&byte, sizeof(byte));
}

size_t LoRaClass::write(const uint8_t *buffer, size_t size)
{


  int currentLength = 0;
  if (_FskMode)
	currentLength = readRegister(REG_PAYLOADLENGTH);
  else
	currentLength = readRegister(REG_PAYLOAD_LENGTH);

  // check size
  if ((currentLength + size) > MAX_PKT_LENGTH) {
    size = MAX_PKT_LENGTH - currentLength;
  }

  // write data
  for (size_t i = 0; i < size; i++) {
    writeRegister(REG_FIFO, buffer[i]);
  }

  // update length
  if (!_FskMode)
	  writeRegister(REG_PAYLOADLENGTH, currentLength);
  else
	  writeRegister(REG_PAYLOAD_LENGTH, currentLength + size);

  return size;
}

int LoRaClass::available()
{
  return (readRegister(REG_RX_NB_BYTES) - _packetIndex);
}

int LoRaClass::read()
{
  if (!available()) {
    return -1;
  }

  _packetIndex++;

  return readRegister(REG_FIFO);
}

int LoRaClass::peek()
{
  if (!available()) {
    return -1;
  }

  // store current FIFO address
  int currentAddress = readRegister(REG_FIFO_ADDR_PTR);

  // read
  uint8_t b = readRegister(REG_FIFO);

  // restore FIFO address
  writeRegister(REG_FIFO_ADDR_PTR, currentAddress);

  return b;
}

void LoRaClass::flush()
{
}

#ifndef ARDUINO_SAMD_MKRWAN1300
void LoRaClass::onReceive(void(*callback)(int))
{
  _onReceive = callback;

  if (callback) {
    pinMode(_dio0, INPUT);

    writeRegister(REG_DIO_MAPPING_1, 0x00);
#ifdef SPI_HAS_NOTUSINGINTERRUPT
    SPI.usingInterrupt(digitalPinToInterrupt(_dio0));
#endif
    attachInterrupt(digitalPinToInterrupt(_dio0), LoRaClass::onDio0Rise, RISING);
  } else {
    detachInterrupt(digitalPinToInterrupt(_dio0));
#ifdef SPI_HAS_NOTUSINGINTERRUPT
    SPI.notUsingInterrupt(digitalPinToInterrupt(_dio0));
#endif
  }
}

void LoRaClass::receive(int size)
{
  if (size > 0) {
    implicitHeaderMode();
	
	if (_FskMode)
		writeRegister(REG_PAYLOADLENGTH, size & 0xff);
	else
		writeRegister(REG_PAYLOAD_LENGTH, size & 0xff);
  } else {
    explicitHeaderMode();
  }

  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}
#endif

void LoRaClass::idle()
{
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}

void LoRaClass::sleep()
{
  //log_i("go to sleep");
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
}

void LoRaClass::setTxPower(int level, int outputPin)
{
  if (PA_OUTPUT_RFO_PIN == outputPin) {
    // RFO
    if (level < 0) {
      level = 0;
    } else if (level > 14) {
      level = 14;
    }
    //log_i("level <= 14 %i\n",level);
    writeRegister(REG_PA_CONFIG, 0x70 | level);
  } else {
    // PA BOOST
    if (level > 17) {
      if (level > 20) {
        level = 20;
      }
      // subtract 3 from level, so 18 - 20 maps to 15 - 17
      level -= 3;
      //log_i("level > 17 %i\n",level);

      // High Power +20 dBm Operation (Semtech SX1276/77/78/79 5.4.3.)
      writeRegister(REG_PA_DAC, 0x87);
      setOCP(140);
    } else {
      if (level < 2) {
        level = 2;
      }
      //log_i("level < 17 %i\n",level);
      //Default value PA_HF/LF or +17dBm
      writeRegister(REG_PA_DAC, 0x84);
      setOCP(100);
    }

    writeRegister(REG_PA_CONFIG, PA_BOOST | (level - 2));
  }
}

void LoRaClass::setFrequency(long frequency)
{
  _frequency = frequency;

  uint64_t frf = ((uint64_t)frequency << 19) / 32000000;

  writeRegister(REG_FRF_MSB, (uint8_t)(frf >> 16));
  writeRegister(REG_FRF_MID, (uint8_t)(frf >> 8));
  writeRegister(REG_FRF_LSB, (uint8_t)(frf >> 0));
}

int LoRaClass::getSpreadingFactor()
{
  return readRegister(REG_MODEM_CONFIG_2) >> 4;
}

void LoRaClass::setSpreadingFactor(int sf)
{
  if (sf < 6) {
    sf = 6;
  } else if (sf > 12) {
    sf = 12;
  }

  if (sf == 6) {
    writeRegister(REG_DETECTION_OPTIMIZE, 0xc5);
    writeRegister(REG_DETECTION_THRESHOLD, 0x0c);
  } else {
    writeRegister(REG_DETECTION_OPTIMIZE, 0xc3);
    writeRegister(REG_DETECTION_THRESHOLD, 0x0a);
  }

  writeRegister(REG_MODEM_CONFIG_2, (readRegister(REG_MODEM_CONFIG_2) & 0x0f) | ((sf << 4) & 0xf0));
  setLdoFlag();
}

long LoRaClass::getSignalBandwidth()
{
  byte bw = (readRegister(REG_MODEM_CONFIG_1) >> 4);

  switch (bw) {
    case 0: return 7.8E3;
    case 1: return 10.4E3;
    case 2: return 15.6E3;
    case 3: return 20.8E3;
    case 4: return 31.25E3;
    case 5: return 41.7E3;
    case 6: return 62.5E3;
    case 7: return 125E3;
    case 8: return 250E3;
    case 9: return 500E3;
  }

  return -1;
}

void LoRaClass::setSignalBandwidth(long sbw)
{
  int bw;

  if (sbw <= 7.8E3) {
    bw = 0;
  } else if (sbw <= 10.4E3) {
    bw = 1;
  } else if (sbw <= 15.6E3) {
    bw = 2;
  } else if (sbw <= 20.8E3) {
    bw = 3;
  } else if (sbw <= 31.25E3) {
    bw = 4;
  } else if (sbw <= 41.7E3) {
    bw = 5;
  } else if (sbw <= 62.5E3) {
    bw = 6;
  } else if (sbw <= 125E3) {
    bw = 7;
  } else if (sbw <= 250E3) {
    bw = 8;
  } else /*if (sbw <= 250E3)*/ {
    bw = 9;
  }

  writeRegister(REG_MODEM_CONFIG_1, (readRegister(REG_MODEM_CONFIG_1) & 0x0f) | (bw << 4));
  setLdoFlag();
}

void LoRaClass::setLdoFlag()
{
  // Section 4.1.1.5
  long symbolDuration = 1000 / ( getSignalBandwidth() / (1L << getSpreadingFactor()) ) ;

  // Section 4.1.1.6
  boolean ldoOn = symbolDuration > 16;

  uint8_t config3 = readRegister(REG_MODEM_CONFIG_3);
  bitWrite(config3, 3, ldoOn);
  writeRegister(REG_MODEM_CONFIG_3, config3);
}

void LoRaClass::setCodingRate4(int denominator)
{
  if (denominator < 5) {
    denominator = 5;
  } else if (denominator > 8) {
    denominator = 8;
  }

  int cr = denominator - 4;

  writeRegister(REG_MODEM_CONFIG_1, (readRegister(REG_MODEM_CONFIG_1) & 0xf1) | (cr << 1));
}

void LoRaClass::setPreambleLength(long length)
{
  if (_FskMode)
  {
    writeRegister(REG_PREAMBLEMSB, (uint8_t)(length >> 8));
    writeRegister(REG_PREAMBLELSB, (uint8_t)(length >> 0));
  }else {
    
    writeRegister(REG_PREAMBLE_MSB, (uint8_t)(length >> 8));
    writeRegister(REG_PREAMBLE_LSB, (uint8_t)(length >> 0));
  }
}

void LoRaClass::setSyncWord(int sw)
{
  writeRegister(REG_SYNC_WORD, sw);
}

void LoRaClass::enableCrc()
{
  if (_FskMode)
    SPIsetRegValue(REG_PACKETCONFIG1, SX127X_CRC_ON, 4, 4);
  else
    writeRegister(REG_MODEM_CONFIG_2, readRegister(REG_MODEM_CONFIG_2) | 0x04);

}

void LoRaClass::disableCrc()
{
   if (_FskMode)
    SPIsetRegValue(REG_PACKETCONFIG1, SX127X_CRC_ON, 4, 4);
   else
    writeRegister(REG_MODEM_CONFIG_2, readRegister(REG_MODEM_CONFIG_2) & 0xfb);
}

void LoRaClass::enableInvertIQ()
{
  writeRegister(REG_INVERTIQ,  0x66);
  writeRegister(REG_INVERTIQ2, 0x19);
}

void LoRaClass::disableInvertIQ()
{
  writeRegister(REG_INVERTIQ,  0x27);
  writeRegister(REG_INVERTIQ2, 0x1d);
}

void LoRaClass::setOCP(uint8_t mA)
{
  uint8_t ocpTrim = 27;

  if (mA <= 120) {
    ocpTrim = (mA - 45) / 5;
  } else if (mA <=240) {
    ocpTrim = (mA + 30) / 10;
  }

  writeRegister(REG_OCP, 0x20 | (0x1F & ocpTrim));
}

byte LoRaClass::random()
{
  return readRegister(REG_RSSI_WIDEBAND);
}

void LoRaClass::setPins(int ss, int reset, int dio0)
{
  _ss = ss;
  _reset = reset;
  _dio0 = dio0;
}

void LoRaClass::setSPI(SPIClass& spi)
{
  _spi = &spi;
}

void LoRaClass::setSPIFrequency(uint32_t frequency)
{
  _spiSettings = SPISettings(frequency, MSBFIRST, SPI_MODE0);
}

void LoRaClass::dumpRegisters(Stream& out)
{
  for (int i = 0; i < 128; i++) {
    out.print("0x");
    out.print(i, HEX);
    out.print(": 0x");
    out.println(readRegister(i), HEX);
  }
}

void LoRaClass::explicitHeaderMode()
{
  _implicitHeaderMode = 0;

  writeRegister(REG_MODEM_CONFIG_1, readRegister(REG_MODEM_CONFIG_1) & 0xfe);
  //GX Bit2 nicht Bit 0
  //writeRegister(REG_MODEM_CONFIG_1, readRegister(REG_MODEM_CONFIG_1) & 0xfb);
}

void LoRaClass::implicitHeaderMode()
{
  _implicitHeaderMode = 1;

  writeRegister(REG_MODEM_CONFIG_1, readRegister(REG_MODEM_CONFIG_1) | 0x01);
  //GX Bit2 nicht Bit 0
  //writeRegister(REG_MODEM_CONFIG_1, readRegister(REG_MODEM_CONFIG_1) | 0x04);
}

void LoRaClass::handleDio0Rise()
{
  int irqFlags = readRegister(REG_IRQ_FLAGS);

  // clear IRQ's
  writeRegister(REG_IRQ_FLAGS, irqFlags);

  if ((irqFlags & IRQ_PAYLOAD_CRC_ERROR_MASK) == 0) {
    // received a packet
    _packetIndex = 0;

    // read packet length
	int packetLength = 0;
	
	if (_FskMode)
		packetLength = _implicitHeaderMode ? readRegister(REG_PAYLOADLENGTH) : readRegister(REG_RX_NB_BYTES);
	else
		packetLength = _implicitHeaderMode ? readRegister(REG_PAYLOAD_LENGTH) : readRegister(REG_RX_NB_BYTES);

    // set FIFO address to current RX address
    writeRegister(REG_FIFO_ADDR_PTR, readRegister(REG_FIFO_RX_CURRENT_ADDR));

    if (_onReceive) {
      _onReceive(packetLength);
    }
  }
}

uint8_t LoRaClass::readRegister(uint8_t address)
{
  return singleTransfer(address & 0x7f, 0x00);
}

void LoRaClass::writeRegister(uint8_t address, uint8_t value)
{
  singleTransfer(address | 0x80, value);
}

uint8_t LoRaClass::readRegister_burst(uint8_t address, uint8_t *data, int length)
{
	select();

	/* bit 7 cleared to read in registers */
	address &= 0x7F;

  uint8_t * out;

  out=data;  

  _spi->beginTransaction(_spiSettings);
  _spi->transfer(address);
  _spi->transferBytes( data, out, length);
	_spi->endTransaction();
	unselect();

	return 0;
}

float LoRaClass::get_airlimit(void)
{
	static uint32_t last = 0;
	uint32_t current = millis();
	uint32_t dt = current - last;
	last = current;

	/* reduce airtime by 1% */
	sx_airtime -= dt*0.01f;
	if(sx_airtime < 0.0f)
		sx_airtime = 0.0f;

	/* air time over 3min average -> 1800ms air time allowed */
	return sx_airtime / 1800.0f;
}

uint8_t LoRaClass::singleTransfer(uint8_t address, uint8_t value)
{
  uint8_t response;

  digitalWrite(_ss, LOW);

  _spi->beginTransaction(_spiSettings);
  _spi->transfer(address);
  response = _spi->transfer(value);
  _spi->endTransaction();

  digitalWrite(_ss, HIGH);

  return response;
}

ISR_PREFIX void LoRaClass::onDio0Rise()
{
  LoRa.handleDio0Rise();
}


//FSK stuff

bool LoRaClass::setFSK() {
   MODE_LONG_RANGE_MODE=0;
   _FskMode=true;
   uint8_t reg = readRegister(REG_OPMODE);
 
  writeRegister(REG_OPMODE,  0x80);
  delay(40); // neet to wait till it shut downs.. 
  writeRegister(REG_OPMODE,  0x00);
  writeRegister(REG_OPMODE,  0x01);


  reg= readRegister(REG_OPMODE);
  //Serial.println( reg,HEX);
  if ((reg & 0x80)==0)
    return true; 
  else 
    return false; 

}

bool LoRaClass::setLoRa() {
  _FskMode=false;
  MODE_LONG_RANGE_MODE=0x80;
   
  uint8_t reg = readRegister(REG_OPMODE);
  /*
  writeRegister(REG_OPMODE,  0xF8 & reg);
  writeRegister(REG_OPMODE,  (0xF8 & reg)|0x80);
  */

  writeRegister(REG_OPMODE,  0x00);
  //delay(10);
  writeRegister(REG_OPMODE,  0x00);
  //delay(10);
  writeRegister(REG_OPMODE,  0x80);
  writeRegister(REG_OPMODE,  0x80);
  writeRegister(REG_OPMODE,  0x81);


  reg=readRegister(REG_OPMODE);
  //Serial.println( reg,HEX);
  if ((reg&0x80)==0x80)
    return true; 
  else 
    return false; 
}

void LoRaClass::WaitTxDone()
{
  u_short exitcnt=0;
  while((readRegister(REG_IRQFLAGS2)&0x08)!=0x08)
  {
    delay(1);
    exitcnt++;
    //if more than 1 second exit..
    if (exitcnt>1000)
      break;
  }
}

void LoRaClass::ClearIRQ()
{
  writeRegister(REG_IRQFLAGS1,0xFF);
  writeRegister(REG_IRQFLAGS2,0xFF);
}

void LoRaClass::SetTxIRQ()
{
  writeRegister(REG_DIOMAPPING1,0x34);
}

void LoRaClass::SetFifoTresh()
{
  writeRegister(REG_FIFOTHRESH,0x80);
}

void LoRaClass::setTXFSK() {
   SPIsetRegValue(REG_OPMODE, 0, 7, 7);
   writeRegister(REG_OPMODE, 0x08| MODE_TX); 
}

void LoRaClass::setRXFSK() {
   SPIsetRegValue(REG_OPMODE, 0, 7, 7);
   writeRegister(REG_OPMODE,  MODE_RX_SINGLE); 
}


void LoRaClass::SPIsetRegValue(uint8_t reg, uint8_t value, uint8_t msb, uint8_t lsb) {
  if((msb > 7) || (lsb > 7) || (lsb > msb)) {
    return;
  }

  uint8_t currentValue = readRegister(reg);
  uint8_t mask = ~((0b11111111 << (msb + 1)) | (0b11111111 >> (8 - lsb)));
  uint8_t newValue = (currentValue & ~mask) | (value & mask);
  writeRegister(reg, newValue);
}


void LoRaClass::setFrequencyDeviation(float freqDev) { 
  // set mode to STANDBY
  writeRegister(REG_OPMODE,  MODE_STDBY);
 
  // set allowed frequency deviation
  uint32_t base = 1;
  uint32_t FDEV = (freqDev * (base << 19)) / 32000;
  writeRegister(REG_FDEVMSB, (FDEV & 0xFF00) >> 8);
  writeRegister(REG_FDEVLSB, FDEV & 0x00FF);
}

void LoRaClass::setBitRate(float br) {
  // set mode to STANDBY
  writeRegister(REG_OPMODE,  MODE_STDBY);
 
  // set bit rate
  uint16_t bitRate = (SX127X_CRYSTAL_FREQ * 1000.0) / br;
  SPIsetRegValue(REG_BITRATEMSB, (bitRate & 0xFF00) >> 8, 7, 0);
  SPIsetRegValue(REG_BITRATELSB, bitRate & 0x00FF, 7, 0);
}

void LoRaClass::setSyncWordFSK(uint8_t * sw, int len)
{
   // enable sync word recognition
  SPIsetRegValue(REG_SYNCCONFIG, SX127X_SYNC_ON, 4, 4);
  SPIsetRegValue(REG_SYNCCONFIG, len - 1, 2, 0);
  
  int i=0;
    for (i=0; i < len; i++) {
      writeRegister((REG_SYNCVALUE1 + i), sw[i]);
    }
}

void  LoRaClass::setPacketMode(uint8_t mode, uint8_t len) {
 
  // set to fixed packet length
  SPIsetRegValue(REG_PACKETCONFIG1, mode, 7, 7);
  // set length to register
  writeRegister(REG_PAYLOADLENGTH, len);
}


void LoRaClass::setPreamblePolarity(bool preamble_polarity)
{
   if (preamble_polarity)
   SPIsetRegValue(REG_SYNCCONFIG, SX127X_PREAMBLE_POLARITY_55, 5, 5);
   else 
    SPIsetRegValue(REG_SYNCCONFIG, SX127X_PREAMBLE_POLARITY_AA, 5, 5);
}


void LoRaClass::setEncoding(uint8_t encoding) {
 
  // set encoding
  switch(encoding) {
    case RADIOLIB_ENCODING_NRZ:
    SPIsetRegValue(REG_PACKETCONFIG1, SX127X_DC_FREE_NONE, 6, 5);
      return;
    case RADIOLIB_ENCODING_MANCHESTER:
      SPIsetRegValue(REG_PACKETCONFIG1, SX127X_DC_FREE_MANCHESTER, 6, 5);
    return;
    case RADIOLIB_ENCODING_WHITENING:
    SPIsetRegValue(REG_PACKETCONFIG1, SX127X_DC_FREE_WHITENING, 6, 5);
      return;
  }
}


void LoRaClass::setRxBandwidth(float rxBw) {

 writeRegister(REG_OPMODE,  MODE_STDBY);
 
  // calculate exponent and mantissa values
  for(uint8_t e = 7; e >= 1; e--) {
    for(int8_t m = 2; m >= 0; m--) {
      float point = (SX127X_CRYSTAL_FREQ * 1000000.0)/(((4 * m) + 16) * ((uint32_t)1 << (e + 2)));
      if(abs(rxBw - ((point / 1000.0) + 0.05)) <= 0.5) {
        // set Rx bandwidth during AFC
        SPIsetRegValue(REG_AFCBW, (m << 3) | e, 4, 0);
        // set Rx bandwidth
        SPIsetRegValue(REG_RXBW, (m << 3) | e, 4, 0);
        return;
      }
    }
  }
  return;
}

void LoRaClass::irqEnable(bool enable)
{
  if (enable)
  {
   attachInterrupt(digitalPinToInterrupt(_dio0), LoRaClass::onDio0Rise, RISING);
  } else {
    detachInterrupt(digitalPinToInterrupt(_dio0));
  }
}

void LoRaClass::setPaRamp(uint8_t ramp)
{
  uint8_t value = 0b00010000| (ramp &0x0F);
  writeRegister(REG_PARAMP, value); // unused=000, LowPnTxPllOff=1, PaRamp=1000
}

LoRaClass LoRa;