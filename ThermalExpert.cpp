//TBD:	probably encapsulated the TE stuff in some static lib (incl. all cuda code and just link to this
//		maybe even include a small viewer window too (GL based)?

//#define CUDA 	//8->12 wait cycles (V1/i5) -4ms? only
//71->85 wait cycles (Q1/i5) -14ms?! on smaller frame?

#include <stdio.h>
#include <iostream>
#include <math.h>
#include <algorithm> //std::sort

#include "ThermalExpert.hpp"
#include <wx/rawbmp.h>
#include "cppcolormap.hpp"

#include <unistd.h> //usleep

using namespace std; //just for debug output ftm.

//Q1 defaults
int imW = 384;
int imH = 288;
int rawH = 296;

//Constructor
ThermalExpert::ThermalExpert()
{
	_dead = NULL;
	_gain = NULL;
	_offset = NULL;
	_userOffset = NULL;
	_rawFrame = NULL;
	_displayFrame = NULL;
	_frame = NULL;

	_activeNUC = false;
	_countNUC = 0;
	_maxNUCframes = 16; //default - 0.55s V1; 1.95s Q1

	useCuda = false;
}

//Destructor
ThermalExpert::~ThermalExpert()
{
	try
	{
		if (_frame)
		{
			delete[] _frame;
		}
		if (_displayFrame)
		{
			delete[] _displayFrame;
		}
		if (_dead)
		{
			delete[] _dead;
		}
		if (_gain)
		{
			delete[] _gain;
		}
		if (_offset)
		{
			delete[] _offset;
		}
		if (_rawFrame)
		{
			delete[] _rawFrame;
		}
		if (_userOffset)
		{
			delete[] _userOffset;
		}
	}
	catch (const std::exception &ex)
	{
		std::cout << ex.what() << std::endl;
	}
}

//connect to ThermalExpert camera
int ThermalExpert::Connect()
{
	//init internals
	//TBD: do this after init command to cam!
	_w = imW;
	_h = imH;
	_hRaw = rawH;
	_fNo = 0; //reset frame number on connect
	ctx = NULL;
	dev_handle = NULL;

	libusb_device **devs;  //pointer to pointer of device, used to retrieve a list of devices
	int r;		       //for return values
	ssize_t cnt;	       //holding number of devices in list
	r = libusb_init(&ctx); //initialize the library for the session we just declared
	if (r < 0)
	{
		cout << "Init Error " << r << endl; //there was an error
		ctx = NULL;
		return 1;
	}
	//removed ftm. as a lot of 'errors' on PI on empty controltransfers for frame read...

	cnt = libusb_get_device_list(ctx, &devs); //get the list of devices
	if (cnt < 0)
	{
		cout << "Get Device Error" << endl; //there was an error
		libusb_exit(ctx);
		ctx = NULL;
		return 1;
	}
	cout << cnt << " Devices in list." << endl;

	//TE-Q1 vID=1352/0x0547 pID=128/0x0080
	dev_handle = libusb_open_device_with_vid_pid(ctx, 1351, 128); //these are vendorID and productID I found for my usb device
	if (dev_handle == NULL)
	{
		cout << "Cannot open device - exit" << endl;
		libusb_free_device_list(devs, 1); //free the list, unref the devices in it
		libusb_exit(ctx);		  //needs to be called to end the
		ctx = NULL;
		dev_handle = NULL;
		return 1;
	}

	cout << "TE-Q1 opened" << endl;
	libusb_free_device_list(devs, 1); //free the list, unref the devices in it

	//int actual; //used to find out how many bytes were written
	if (libusb_kernel_driver_active(dev_handle, 0) == 1)
	{ //find out if kernel driver is attached
		cout << "Kernel Driver Active" << endl;
		if (libusb_detach_kernel_driver(dev_handle, 0) == 0) //detach it
			cout << "Kernel Driver Detached!" << endl;
	}
	r = libusb_claim_interface(dev_handle, 0); //claim interface 0 (the first) of device (mine had jsut 1)
	if (r < 0)
	{
		cout << "Cannot claim interface.\nError code: " << r << "\nError name: " << libusb_error_name(r) << endl;
		libusb_close(dev_handle); //close the device we opened
		libusb_exit(ctx);	  //needs to be called to end the
		ctx = NULL;
		dev_handle = NULL;
		return 2;
	}
	cout << "Claimed TE-Q1 interface" << endl;

	//NOW: try to read dead pixel map
	_dead = new unsigned char[_w * _h];
	if (ReadDeadPixelData(_dead))
		cout << "Dead pixel correction available" << endl;
	else
	{
		delete[] _dead;
		_dead = NULL;
	}
	//try reading gain/offset values
	_gain = new float[_w * _h * 4];
	_offset = new float[_w * _h * 4];
	if (ReadGainOffsetData(_gain, _offset))
	{
		cout << "Shutterless correction available" << endl;
	}
	else
	{
		delete[] _gain;
		delete[] _offset;
		_gain = NULL;
		_offset = NULL;
	}

	//setup frame buffers
	int rawSize = _w * _hRaw * 2; //raw frame size in bytes
	int frameSize = _w * _hRaw;   //number of actual pixels
	_rawFrame = new unsigned char[rawSize];
	_frame = new short[frameSize];
	_displayFrame = new unsigned char[frameSize];
	bmpFrame = new wxBitmap(_w, imH, 24);
	bmpColourMap = new wxBitmap(256, 16, 24);

	//if we could not read the calibration data from file try to retrieve from camera first frames
	if (!_gain || !_offset || !_dead)
	{
		std::cout << "Warning: Could not read flash data from disk." << std::endl;
		if (dev_handle == NULL)
		{
			std::cout << "Error: no active device!" << std::endl;
			return -1;
		}
		else
		{
			std::cout << "Attempting to read flash data from camera." << std::endl;
			int initCnt = 0;
			//frame 0 -> dead pixels
			//frame 1 -> ??? ignore
			//frame 2-9 -> gain map
			//frame 10-17 -> offset map
			//frame 18-25 -> display offsets and corresponding FPA temperatures
			//frame 26 -> ??? ignore
			while (initCnt < 27)
			{
				int len = ReadRawFrame(dev_handle, _w * _hRaw * 2, _rawFrame); //increments _fNo!
				if (len > 0)
				{ //>= (_w*_hRaw*2)){ //
					if (initCnt == 0)
					{ //dead pixels
						//std::cout << "Frame first val = " << firstVal << std::endl;
						//if (firstVal > 16383)
						getDeadPixels(); //WORKs
								 //else
								 //{
								 //	std::cout << "Error: no flash data available!" << std::endl;
								 //	std::cout << "       Disconnect and re-connect camera, then restart program." << std::endl;
								 //	return -1; //exit loop -> no shutterless data available (need to disconnect cam OR send appropriate command to camera to re-read data!)
								 //}
					}
					else if (initCnt > 17)
					{ //display offset and FPA temps
						getShutterlessDisplayOffsets(initCnt - 18);
					}
					else if (initCnt > 9)
					{ //offsets
						getShutterlessOffsets(initCnt - 10);
					}
					else if (initCnt > 1)
					{ //gains
						getShutterlessGains(initCnt - 2);
					}
					initCnt++;
				}
			}
		}
	}
	return 0;
}

//extract dead pixel map from flash read frame 0
void ThermalExpert::getDeadPixels()
{
	int cdsOffset = _w * 4;
	int frameSize = _w * _h;
	int _deadCount = 0;
	_dead = new unsigned char[frameSize];
	//find dead pixels (according to flash data)
	for (int i = cdsOffset; i < (cdsOffset + frameSize); i++)
	{
		unsigned short val = (unsigned short)(_rawFrame[2 * i] << 8 | _rawFrame[2 * i + 1]);
		if (val > 0)
		{
			_dead[i - cdsOffset] = 1;
			_deadCount++;
		}
		else
			_dead[i - cdsOffset] = 0;
	}
	//dump to disk!
	const char *name = "Q1.dead";
	FILE *df = fopen(name, "wb");
	fwrite(_dead, sizeof(unsigned char), frameSize, df);
	std::fclose(df);
}

//extract shutterless correction gains from flash read frames 2...9
void ThermalExpert::getShutterlessGains(int seg)
{
	if (seg == 0)
		_gain = new float[_w * _h * 4];

	//first reorder bytes
	for (int i = 0; i < _w * _hRaw; i++)
	{ //reorder in-place = just flip two consecutive bytes
		unsigned char tmp = _rawFrame[2 * i];
		_rawFrame[2 * i] = _rawFrame[2 * i + 1];
		_rawFrame[2 * i + 1] = tmp;
	}
	//get correct offset
	int gainFrame = seg;
	int mapOffset = (_h / 8 * gainFrame) * _w * 4; //36 lines per frame w/ 4 values per pixel!
	int mapOffsetBytes = mapOffset * 4;	       //offset in bytes
	int CDSoffset = _w * 4 * 2;		       //4CDS lines
	int Length = _w * _h * 2;
	//to floats and directly copy into gain map
	//Buffer.BlockCopy(raw, CDSoffset, _flashGain, mapOffsetBytes, Length);
	unsigned char *gPtr = (unsigned char *)_gain; //map to uchar pointer
	for (int i = 0; i < Length; i++)
	{
		gPtr[mapOffsetBytes + i] = _rawFrame[CDSoffset + i]; //direct copy to correct place
	}

	if (seg == 7)
	{
		//dump to disk!
		const char *name = "Q1.gain";
		FILE *df = fopen(name, "wb");
		fwrite(_gain, sizeof(float), _w * _h * 4, df);
		std::fclose(df);
	}
}

//extract shutterless correction offsets from flash read frames 10...17
void ThermalExpert::getShutterlessOffsets(int seg)
{
	if (seg == 0)
		_offset = new float[_w * _h * 4];

	//first reorder bytes
	for (int i = 0; i < _w * _hRaw; i++)
	{ //reorder in-place = just flip two consecutive bytes
		unsigned char tmp = _rawFrame[2 * i];
		_rawFrame[2 * i] = _rawFrame[2 * i + 1];
		_rawFrame[2 * i + 1] = tmp;
	}
	//get correct offset
	int offFrame = seg;
	int mapOffset = (_h / 8 * offFrame) * _w * 4; //36 lines per frame w/ 4 values per pixel!
	int mapOffsetBytes = mapOffset * 4;	      //offset in bytes
	int CDSoffset = _w * 4 * 2;		      //4CDS lines
	int Length = _w * _h * 2;
	//to floats and directly copy into gain map
	//Buffer.BlockCopy(raw, CDSoffset, _flashGain, mapOffsetBytes, Length);
	unsigned char *oPtr = (unsigned char *)_offset; //map to uchar pointer
	for (int i = 0; i < Length; i++)
	{
		oPtr[mapOffsetBytes + i] = _rawFrame[CDSoffset + i]; //direct copy to correct place
	}

	if (seg == 7)
	{
		//dump to disk!
		const char *name = "Q1.offset";
		FILE *df = fopen(name, "wb");
		fwrite(_offset, sizeof(float), _w * _h * 4, df);
		std::fclose(df);
	}
}

//should extract additional display offsets and temp calibration data from flash read frames number 18...24
//BUT: currently just reading temperature calibration data only
void ThermalExpert::getShutterlessDisplayOffsets(int seg)
{
	//reorder first 4 bytes
	unsigned char reordered[4];
	int offset = 2 * 1530;
	reordered[0] = _rawFrame[offset + 1]; //those are used in other segments than first one!
	reordered[1] = _rawFrame[offset + 0];
	reordered[2] = _rawFrame[offset + 3];
	reordered[3] = _rawFrame[offset + 2];
	if (seg == 0)
	{
		_factoryFPATemp[0] = *(float *)reordered; //byte to float 'cast'
		//get temp gain and offset from somewhere in frame
		offset = 2 * 1532;
		reordered[0] = _rawFrame[offset + 1];
		reordered[1] = _rawFrame[offset + 0];
		reordered[2] = _rawFrame[offset + 3];
		reordered[3] = _rawFrame[offset + 2];
		_tempOffset = *(float *)reordered; //byte to float 'cast'
		//get temp gain
		offset = 2 * 1534;
		reordered[0] = _rawFrame[offset + 1];
		reordered[1] = _rawFrame[offset + 0];
		reordered[2] = _rawFrame[offset + 3];
		reordered[3] = _rawFrame[offset + 2];
		_tempGain = *(float *)reordered; //byte to float 'cast'
	}
	else if (seg == 2)
		_factoryFPATemp[1] = *(float *)reordered; //byte to float 'cast'
	else if (seg == 4)
		_factoryFPATemp[2] = *(float *)reordered; //byte to float 'cast'
	else if (seg == 6)
	{
		_factoryFPATemp[3] = *(float *)reordered; //byte to float 'cast'
		//dump to disk!
		const char *name = "Q1.fpa";
		FILE *df = fopen(name, "wb");
		fwrite(_factoryFPATemp, sizeof(float), 4, df); //4x factory FPA temperature
		fwrite(&_tempGain, sizeof(float), 1, df);
		fwrite(&_tempOffset, sizeof(float), 1, df);
		std::fclose(df);
		//now calc the ambient correction factors...
		_validCal = true; //set flag for valid data
		getAmbientCorrectionData();
	}
}

//compute ambient correction data from available flash data (factoryFPATemp, tempGain & tempOffset)
void ThermalExpert::getAmbientCorrectionData()
{
	float sortFPATemp[4];
	for (int i = 0; i < 4; i++)
		sortFPATemp[i] = _factoryFPATemp[i];

	std::sort(sortFPATemp, sortFPATemp + 4);
	float multiChamberTemp[] = {5.0f, 25.0f, 35.0f};

	for (int i = 0; i < 2; i++)
	{
		if (fabsf(sortFPATemp[i + 1] - sortFPATemp[i]) < 1e-10f)
		{
			_ambientConstA[i] = 1.0f;
			_ambientConstB[i] = 0.0f;
		}
		else
		{
			_ambientConstA[i] = (multiChamberTemp[i + 1] - multiChamberTemp[i]) / (sortFPATemp[i + 1] - sortFPATemp[i]);
			_ambientConstB[i] = multiChamberTemp[i] - _ambientConstA[i] * sortFPATemp[i];
		}
	}

	_ambientCorrConstA[0] = 0.6666666666666666f;
	_ambientCorrConstA[1] = 1.0f;
	_ambientCorrConstB[0] = 1.6666666666666667f;
	_ambientCorrConstB[1] = -5.0f;
}

//compute estimation of ambient temperatur (lens temp?!)
float ThermalExpert::getAmbientTemp(float fpaTemp)
{
	if (fpaTemp >= _factoryFPATemp[1])
	{
		return (_ambientConstA[1] * fpaTemp) + _ambientConstB[1];
	}
	return (_ambientConstA[0] * fpaTemp) + _ambientConstB[0];
}

//compute ambient correction temperature (from ambient temp estimation)
float ThermalExpert::getAmbientCorrTemp(float ambientTemp)
{
	return (-0.2f * ambientTemp) + 5.0f;
}

//do DL to temperature calibration for given corrected DL value
float ThermalExpert::getTargetTemp(float dValue)
{
	if ((dValue * 3.240962 - 22489.06934208) < 0)
	{
		return 0.0;
	}
	//return ((((double)_tempGain * (-411.744319 + Math.Sqrt(169533.38422877376 - ((7146.4357337 - dValue) * 9.8736116)))) / 4.9368058) - (double)_tempOffset) + (double)_tempTestOffset;
	//simpler 10 ops -> 7 ops
	//return (double)_tempGain * (Math.Sqrt(4060.89136328 + dValue * 0.40512025002) - 83.4029807) - (double)_tempOffset + (double)_tempTestOffset;
	//adapted to new correction d_new = 0.125*d + 65536
	return _tempGain * (sqrtf(dValue * 3.240962f - 22489.06934208f) - 83.4029807f) - _tempOffset + _tempTestOffset;
}

//compute temperature around given image location
float ThermalExpert::getPointTemp(int x, int y)
{
	if (_frame && _validCal) //we assume this is corrected - if not strange values may/will result!
	{
		//bounds check/clamp
		x = x < 1 ? 1 : x;
		x = x >= _w - 1 ? _w - 2 : x;
		y = y < 1 ? 1 : y;
		y = y >= _h - 1 ? _h - 2 : y;
		float dVal = 0.0f;
		//average DL on 3x3 patch
		for (int i = -1; i < 2; ++i)
		{
			int xx = x + i;
			for (int j = -1; j < 2; ++j)
			{
				int yy = y + j;
				dVal += (float)_frame[yy * _w + xx];
			}
		}
		dVal /= 9.0f;
		float ambientCorrTemp = getAmbientCorrTemp(getAmbientTemp(_fpaTemp));
		return getTargetTemp(dVal) + ambientCorrTemp;
	}
	return 0.0;
}

//return last FPA temperature
float ThermalExpert::getFPATemp()
{
	return _fpaTemp;
}

//return last image minimum temperature
float ThermalExpert::getMinTemp()
{
	return _minTemp;
}

//return last image maximum temperature
float ThermalExpert::getMaxTemp()
{
	return _maxTemp;
}

//disconnect ThermalExpert camera
int ThermalExpert::Disconnect()
{
	//send disconnect command
	//C#: byte[] byte1 = device.ControlTransferIn(0x80, 0x33, 0x1, 0xbeef, 0x10);
	if (dev_handle != NULL)
	{
		unsigned char ctrl[16];
		int ret = libusb_control_transfer(dev_handle, 0x80, 0x33, 0x1, 0xbeef, ctrl, 0x10, 500);
		cout << "Disconnected TE-Q1" << endl;
		usleep(100 * 1000); //wait a little bit?!
		//release the claimed interface
		ret = libusb_release_interface(dev_handle, 0);
		if (ret != 0)
		{
			cout << "Cannot Release Interface" << endl;
			libusb_close(dev_handle); //close the device we opened
			libusb_exit(ctx);	  //needs to be called to end the context
			return 1;
		}
		cout << "Released Interface" << endl;
		libusb_close(dev_handle); //close the device we opened
		libusb_exit(ctx);	  //needs to be called to end the context
	}
	return 0;
}

//read dead pixel data from disk
bool ThermalExpert::ReadDeadPixelData(unsigned char *deadPixels)
{ //works
	const char *fileName = "Q1.dead";
	size_t size = imW * imH; //fixed ftm.

	FILE *f = fopen(fileName, "rb");
	size_t len = 0;
	if (f)
		len = fread(deadPixels, 1, size, f);
	else
		return false;

	std::fclose(f);

	if (len == size)
		return true;

	cout << "V1.dead expected: " << size << " - read: " << len << endl;
	return false;
}

//read shutterless correction gain and offset data from disk
bool ThermalExpert::ReadGainOffsetData(float *gains, float *offsets)
{
	const char *fileGain = "Q1.gain";
	const char *fileOffset = "Q1.offset";
	const char *fileFPA = "Q1.fpa";

	size_t size = imW * imH * 4 * 4; //fixed ftm.

	cout << "Reading gains ... ";
	FILE *f = fopen(fileGain, "rb");
	if (f == NULL)
	{
		cout << "error opening V1.gain" << endl;
		return false;
	}
	size_t len = fread(gains, 1, size, f);
	std::fclose(f);
	if (len == size)
		cout << "success" << endl;
	else
	{
		cout << "error" << endl;
		return false;
	}

	cout << "Reading offset ... ";
	f = fopen(fileOffset, "rb");
	if (f == NULL)
	{
		cout << "error opening V1.offset" << endl;
		return false;
	}
	len = fread(offsets, 1, size, f);
	std::fclose(f);

	if (len == size)
	{
		cout << "success" << endl;
	}
	else
	{
		cout << "error" << endl;
		return false;
	}

	cout << "Reading calibration data ... ";
	f = fopen(fileFPA, "rb");
	if (f)
	{
		int lenT = fread(_factoryFPATemp, sizeof(float), 4, f);
		int lenG = fread(&_tempGain, sizeof(float), 1, f);
		int lenO = fread(&_tempOffset, sizeof(float), 1, f);
		if (lenT == 4 && lenG == 1 && lenO == 1)
		{
			getAmbientCorrectionData();
			_validCal = true;
		}

		return true;
	}
	else
		return false;
}

//get raw frame from USB device
int ThermalExpert::ReadRawFrame(libusb_device_handle *dev_handle, int size, unsigned char *frame)
{		     //works
	int len = 0; //actual length of returned data
	if (dev_handle != NULL)
	{
		//C#: byte[] byte1 = device.ControlTransferIn(0x80, 0x86, 0x1bc, 0xbeef, 0x10); //possibly 0x80 instead of 0xc0?!
		//0x1bc = 444 = 384*296*2/512 -> number of packets
		//for the V1 this is 640*482*2/512 = 1205 = 0x4B5
		uint16_t packets = (uint16_t)(size / 512);
		if (packets * 512 < size)
		{
			packets++;	      //increase number of pakets to receive full frame
			size = packets * 512; //adapt size too!
		}
		unsigned char ctrl[16];
		int ret = libusb_control_transfer(dev_handle, 0x80, 0x86, packets, 0xbeef, ctrl, 0x10, 5);
		//C#: device.ReadExactPipe(0x86, 0x37800); //0x37800 = 384*296*2 bytes
		/* cout<<"get frame ret: " << ret <<endl; //this is always zero!!
        cout << "ctrl :";
        for(int i=0;i<16;i++)
            cout << (int)ctrl[i] << "; "; //more or less arbitrary return here: i.e.: 240; 118; 122; 63; 255; 127; 0; 0; 95; 36; 64; 0; 0; 0; 0; 0;
        cout << endl; */
		ret = libusb_bulk_transfer(dev_handle, 0x86, frame, size, &len, 0); //this takes 13ms (i5) for the V1 !! ~45MB/s?!
										    //NOTE: 5ms is too short! -> only 230kB or 5/13th of the frame!
		//cout << "Read " << len << " bytes from pipe" << endl;
		if (len == size)
		{

			//cout << "frame No = " << _fNo << endl;
#if 0
			if (_fNo < 28)
			{
				//DEBUG: dump raw data to disk
				char buff[256];
				sprintf(buff, "dump%04d.bin", _fNo);
				FILE *f = fopen(buff, "wb");
				fwrite(frame, 1, size, f);
				fclose(f);
			}
#endif
			_fNo++; //also increment frame number -> used by readGain/Offset/FPA/Dead
		}
	}
	return len;
}

//compute FPA temperature from raw frame
float ThermalExpert::GetFPATemperatur(unsigned char *raw)
{
	//compute FPA temperature
	float mean = 0.0f;
	float fpaTemp = 38.0f; //dummy value...
	int count = 0;
	//short *svals = (short*)raw;
	//average one of the lines holding the FPA temp data (there are 2 available)
	for (int line = imH; line < imH + 2; line++) //line 0-287 = image, 288+289 = temperature
	{
		for (int col = 3; col < imW; col++)
		{ //ignore the first 3 values!
			count++;
			int pos = 2 * (imW * line + col);
			unsigned short sval = (unsigned short)((raw[pos + 1] << 8) | raw[pos]); //directly swap byte order
			mean += (float)sval;							//frame[384 * line + col]; //valid values should be ~6500...7600 (-28.2°... +72.4°)
												//mean += (float)(svals[imW*line+col]); //this gives correct results!!
		}
	}
	mean /= (float)count; //average
	if (count != 0)
	{
		fpaTemp = (333.3f * (((mean * 4.5f) / 16384.0f) - 1.75f)) - 40.0f; //Q1
	}
	//std::cout << "FPA temp: " << fpaTemp << " - mean: " << mean << std::endl;
	return fpaTemp;
}

//do full shutterless correction on raw image frame (3rd order gain-offset, user NUC offsets and dead pixel replacement)
int ThermalExpert::ShutterlessCorrectFrame(unsigned char *raw, int imageSizePixels, float fpaTemp, short *frame)
{
	short val = 0;
	int offsetBytes = 0;
	for (int i = 0; i < imageSizePixels; i++)
	{
		int pos = offsetBytes + 2 * i;
		val = (short)((raw[pos + 1] << 8) | raw[pos]); //directly swap byte order
		frame[i] = val;
	}
	_minDL = 32767;
	_maxDL = 0;
	//do shutterless correction if available
	if (_gain != NULL && _offset != NULL)
	{
		//cout << "FPA Temp: " << fpaTemp << endl; //" mean: " << mean << endl;
		//cout << "shuterless correction" << endl;

		float T = fpaTemp; //T
		float T2 = T * T;  //T²
		float T3 = T2 * T; //T³

		for (int i = 0; i < imW * imH; i++)
		{
			float currentPixel;
			float fGain = (float)(_gain[4 * i] + _gain[4 * i + 1] * T + _gain[4 * i + 2] * T2 + _gain[4 * i + 3] * T3);
			float fOffset = (float)(_offset[4 * i] + _offset[4 * i + 1] * T + _offset[4 * i + 2] * T2 + _offset[4 * i + 3] * T3);
			currentPixel = 4.0 * (((float)frame[i] - fOffset) * fGain) + 10240.0;
			if (!_activeNUC && _userOffset != NULL)
				currentPixel += _userOffset[i];

			frame[i] = (short)(currentPixel);
			if (_dead != NULL)
			{
				if (_dead[i] > 0)
				{
					//not the most soffisticated version but it works for the moment...
					int newI = i;
					while (_dead[newI] && newI >= 0)
					{
						newI--;
					}
					frame[i] = frame[newI];
				}
				else
				{ //track frame min/max in parallel
					_minDL = frame[i] < _minDL ? frame[i] : _minDL;
					_maxDL = frame[i] > _maxDL ? frame[i] : _maxDL;
				}
			}
		}
	}

	if (_validCal)
	{
		float ambientCorrTemp = getAmbientCorrTemp(getAmbientTemp(_fpaTemp));
		_minTemp = getTargetTemp(_minDL) + ambientCorrTemp;
		_maxTemp = getTargetTemp(_maxDL) + ambientCorrTemp;
	}
	return 0;
}

//accumulate given frame into NUC frame and compute user offsets
void ThermalExpert::AccumulateNUCFrame(short *frame, int width, int height)
{

	if (_userOffset == NULL)
		_userOffset = new float[width * height];

	if (_countNUC == 0)
	{ //init to zero on first frame
		for (int i = 0; i < width * height; ++i)
		{
			_userOffset[i] = 0.0f;
		}
	}

	for (int i = 0; i < width * height; ++i)
	{
		_userOffset[i] += (float)frame[i]; //accumulate pixel values
	}

	//DumpOffsets(_countNUC, _userOffset, width * height);
	//std::cout << "NUC frame " << _countNUC << endl;
	_countNUC++;

	if (_countNUC >= _maxNUCframes)
	{ //after final frame accumulation convert to actual offsets
		float mean = 0.0;
		//get mean value
		for (int i = 0; i < width * height; ++i)
		{
			mean += _userOffset[i];
		}
		mean /= (float)(width * height);
		//now normalize and convert to offsets (subtract mean)
		float scale = 1.0f / (float)_maxNUCframes;
		for (int i = 0; i < width * height; ++i)
		{
			_userOffset[i] = (mean - _userOffset[i]) * scale;
		}
		std::cout << "NUC finalized - Mean: " << mean << endl;

		_activeNUC = false; //end NUC'ing
	}
}

wxBitmap *ThermalExpert::GetWxBitmap(std::string colourmapname)
{
	if (dev_handle != NULL)
	{
		int len = ReadRawFrame(dev_handle, _w * _hRaw * 2, _rawFrame);
		if (len > 0)
		{
			_fpaTemp = GetFPATemperatur(_rawFrame);
			ShutterlessCorrectFrame(_rawFrame, _w * _h, _fpaTemp, _frame);
			if (_activeNUC)
			{
				AccumulateNUCFrame(_frame, _w, _h);
			}
			wxNativePixelData bitmap(*bmpFrame);
			short minv = 32767; //65535;
			short maxv = 0;
			//get min/max values -> AGC
			for (int i = 0; i < _w * _h; i++)
			{
				minv = _frame[i] < minv ? _frame[i] : minv;
				maxv = _frame[i] > maxv ? _frame[i] : maxv;
			}
			//cout << "AGC min: " << minv << " - max: " << maxv << endl;
			double invDelta = 1.0 / ((double)(maxv - minv));
			wxNativePixelData::Iterator p(bitmap);
			xt::xtensor<double, 2> colourmap = cppcolormap::colormap(colourmapname, 256);
			for (int i = 0; i < _w * _h; i++, p++)
			{
				p.Red() = (unsigned char)(255.0 * colourmap((unsigned char)((255.0 * (_frame[i] - minv)) * invDelta), 0));
				p.Green() = (unsigned char)(255.0 * colourmap((unsigned char)((255.0 * (_frame[i] - minv)) * invDelta), 1));
				p.Blue() = (unsigned char)(255.0 * colourmap((unsigned char)((255.0 * (_frame[i] - minv)) * invDelta), 2));
			}
			return bmpFrame;
		}
		return bmpFrame;
	}
	return NULL;
}

wxBitmap *ThermalExpert::GetColourMapWxBitmap(std::string colourmapname)
{
	wxNativePixelData bitmap(*bmpColourMap);
	wxNativePixelData::Iterator p(bitmap);
	xt::xtensor<double, 2> colourmap = cppcolormap::colormap(colourmapname, 256);
	for (int y = 0; y < bmpColourMap->GetHeight(); ++y)
	{
		wxNativePixelData::Iterator rowStart = p;
		for (int x = 0; x < bmpColourMap->GetWidth(); ++x, ++p)
		{
			p.Red() = (unsigned char)(255.0 * colourmap(x, 0));
			p.Green() = (unsigned char)(255.0 * colourmap(x, 1));
			p.Blue() = (unsigned char)(255.0 * colourmap(x, 2));
		}
		p = rowStart;
		p.OffsetY(bitmap, 1);
	}
	return bmpColourMap;
}

//start user NUC
void ThermalExpert::DoNUC()
{
	_maxNUCframes = 16; //maybe use parameter...
	_countNUC = 0;	    //no frames accumulated yet
	_activeNUC = true;
	//std::cout << "starting NUC" << endl;
}
