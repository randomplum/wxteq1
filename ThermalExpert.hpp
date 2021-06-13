#include <libusb.h>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

class ThermalExpert
{
	libusb_device_handle *dev_handle; //a device handle
	libusb_context *ctx = NULL;	  //a libusb session

	float *_gain;
	float *_offset;
	float *_userOffset; //from NUC
	//additional calibration data
	bool _validCal = false;
	float _factoryFPATemp[4];
	float _ambientConstA[2];
	float _ambientConstB[2];
	float _ambientCorrConstA[2];
	float _ambientCorrConstB[2];
	float _tempGain = 0.0f;	      //gain for double->temperature
	float _tempOffset = 0.0f;     //offset for double->temperature
	float _tempTestOffset = 0.0f; //additional temperature offset for double->temperature
	float _fpaTemp = 300.0f;      //fpa temperature read from last raw frame
	float _minTemp = 300.0f;      //min temperature in last frame
	float _maxTemp = 300.0f;      //max temperature in last frame
	short _minDL = 0;	      //min DL in last frame
	short _maxDL = 32767;	      //max DL in last frame

	unsigned char *_dead;
	unsigned char *_rawFrame;
	unsigned char *_displayFrame;
	short *_frame;
	wxBitmap *bmpFrame;
	wxBitmap *bmpColourMap;

	int _w;
	int _h;
	int _hRaw;
	int _countNUC;
	int _maxNUCframes;

	unsigned int _fNo; //frame number since startup

	bool useCuda;
	bool _activeNUC;

	bool ReadDeadPixelData(unsigned char *deadPixels);
	bool ReadGainOffsetData(float *gains, float *offsets);
	int ReadRawFrame(libusb_device_handle *dev_handle, int size, unsigned char *frame);
	float GetFPATemperatur(unsigned char *raw);
	int ShutterlessCorrectFrame(unsigned char *raw, int imageSizePixels, float fpaTemp, short *frame);
	void AccumulateNUCFrame(short *frame, int width, int height);

	void getDeadPixels();
	void getShutterlessOffsets(int seg);
	void getShutterlessGains(int seg);
	void getShutterlessDisplayOffsets(int seg);

	void getAmbientCorrectionData();
	float getAmbientTemp(float fpaTemp);
	float getAmbientCorrTemp(float temp);
	float getTargetTemp(float dValue);

public:
	ThermalExpert();
	~ThermalExpert();

	int ImageWidth() { return _w; }

	int ImageHeight() { return _h; }

	bool IsConnected() { return dev_handle != NULL; }

	int Connect();

	int Disconnect();

	wxBitmap *GetWxBitmap(std::string colourmapname);
	wxBitmap *GetColourMapWxBitmap(std::string colourmapname);
	float getPointTemp(int x, int y);
	float getFPATemp();
	float getMinTemp();
	float getMaxTemp();

	void DoNUC();
};