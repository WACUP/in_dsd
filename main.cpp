///////////////////////////
// Direct Stream Digital //
//  by David Kharabadze  //
//  License: LGPL v.3    //
///////////////////////////
// Samples are downloaded from:
// http://www.ayre.com/insights_dsdvspcm.htm
// (link from http://pcaudiophile.ru/index.php?id=31 )
///////////////////////////
// more sample:
// https://www.oppodigital.com/hra/dsd-by-davidelias.aspx
///////////////////////////

#define PLUGIN_VERSION L"1.2.11"

//------------------------ External headers
#include<Windows.h>
#include<strsafe.h>
#include<winamp/in2.h>
#include"api.h"
#include<loader/loader/paths.h>
#include<loader/loader/utils.h>
#include<loader/hook/squash.h>

//------------------------ Internal headers
#include"DSD.h"
#include"Decoder.h"
#include"resource.h"


//------------------------ Global constants & parameters
// post this to the main window at end of file (after playback as stopped)
#define BPS 24/*/16/**/
int SAMPLERATE=0;

void about(HWND hwndParent);
int init(void);
void quit(void);
void getfileinfo(const in_char* filename, in_char* title, int* length_in_ms);
int infoDlg(const in_char* fn, HWND hwnd);
//int isourfile(const in_char* fn);
int play(const in_char* fn);
void pause();
void unpause();
int ispaused();
void stop();
int getlength();
int getoutputtime();
void setoutputtime(int time_in_ms);
void setvolume(int volume);
void setpan(int pan);

void __cdecl GetFileExtensions(void);

//------------------------ Connection with plugin
In_Module plugin = { IN_VER_WACUP,
	(char*)L"Direct Stream Digital Player v" PLUGIN_VERSION,
	0,	// hMainWindow
	0,	// hDllInstance
	0,
	1,	// is_seekable
	1,	// uses output
	NULL/*/config/**/,
	/*NULL/*/about/**/,
	init,
	quit,
	getfileinfo,
	0/*infoDlg*/,
	0/*isourfile*/,
	play,
	pause,
	unpause,
	ispaused,
	stop,
	getlength,
	getoutputtime,
	setoutputtime,
	setvolume,
	setpan,
	IN_INIT_VIS_RELATED_CALLS,
	0,0,	// dsp
	IN_INIT_WACUP_EQSET_EMPTY
	NULL,	// setinfo
	0,		// out_mod,
	NULL,	// api_service
	INPUT_HAS_READ_META | INPUT_USES_UNIFIED_ALT3 |
	INPUT_HAS_FORMAT_CONVERSION_UNICODE |
	INPUT_HAS_FORMAT_CONVERSION_SET_TIME_MODE,
	GetFileExtensions,	// loading optimisation
	IN_INIT_WACUP_END_STRUCT };//Procedure addresses & parameters

//------------------------ Global variables start
#ifdef _DEBUG
FILE *debugfile=0;//Debug file for information output
#endif
FILE *f;//Input sound file
tDSD DSD;//DSD parser

volatile int killDecodeThread=0;
HANDLE thread_handle=NULL;

int decode_pos_ms=0;			// current decoding position, in milliseconds.
int paused=0;					// are we paused?
volatile int seek_needed=-1;	// if != -1, it is the point that the decode


//------------------------ Function for size reduce
//BOOL WINAPI _DllMainCRTStartup(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved){
//	return TRUE;
//}

void __cdecl GetFileExtensions(void)
{
	if (!plugin.FileExtensions)
	{
		LPCWSTR extensions[]
		{
			{ L"DSF" },
			{ L"DFF" }
		},
			// TODO localise
			descriptions[]
		{
			{ L"Sony Direct Stream Digital (*.DSF)" },
			{ L"Phillips Direct Stream Digital (*.DFF)" },
		};

		plugin.FileExtensions = BuildInputFileListArrayString(extensions, descriptions,
													ARRAYSIZE(extensions), NULL, NULL);
	}
}

//------------------------- Code start
/*void config(HWND hwndParent){
	MessageBoxW(hwndParent,
		L"No configuration. Auto-detection of DSD type",
		L"Configuration",MB_OK);
	// if we had a configuration box we'd want to write it here (using DialogBox, etc)
}*/

void about(HWND hwndParent){
	// TODO localise
	const unsigned char* output = DecompressResourceText(plugin.hDllInstance, plugin.hDllInstance, IDR_ABOUT_TEXT_GZ);
	wchar_t message[1024]/* = { 0 }*/;
	PrintfCch(message, ARRAYSIZE(message), (LPCWSTR)output, (LPCWSTR)plugin.description,
			  WACUP_Author(), WACUP_Copyright(), TEXT(__DATE__));
	AboutMessageBox(hwndParent, message, L"Direct Stream Digital Player");
	SafeFree((void*)output);
}

int init(void) {
#ifdef _DEBUG
	debugfile=0;//OFF DEBUG
	//if(debugfile==0)fopen_s(&debugfile,"D:/David/DSDdebug.txt","wt");//ON DEBUG
	if(debugfile){fprintf(debugfile,"Start debug\n");fflush(debugfile);}
#endif
	plugin.description = (char*)L"Direct Stream Digital Player v" PLUGIN_VERSION;
	return IN_INIT_SUCCESS;
}

void quit(void) {
	//if((sample_buffer_size!=0)&&(sample_buffer!=0))
	//delete[] sample_buffer;
	//sample_buffer_size=0;
	//sample_buffer=0;
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Finish debug\n");fflush(debugfile);}
	if(debugfile)fclose(debugfile);
#endif
}

void getfileinfo(const in_char *filename, in_char *title, int *length_in_ms){
	title=0;//Dont't decode ID3v2 TAG
	if (!filename || !*filename){  // currently playing file
#ifdef _DEBUG
		if(debugfile){fprintf(debugfile,"Curent File Info\n");fflush(debugfile);}
#endif
		if(length_in_ms)
			*length_in_ms=int(DSD.Samples*1000/DSD.SampleRate);
		if(title)
			wcscpy(title,L"Current_File");//???
#ifdef _DEBUG
		if(debugfile){fprintf(debugfile,"File info:%llu / %i\n",DSD.Samples,DSD.SampleRate);fflush(debugfile);}
#endif
	}else{
#ifdef _DEBUG
		if(debugfile){fprintf(debugfile,"File %s info\n",filename);fflush(debugfile);}
#endif
		tDSD *locDSD=new tDSD;
		FILE *lf = _wfsopen(filename, L"rb", _SH_DENYNO);
		if (lf != 0) {
			locDSD->start(lf);
			fclose(lf);
		}
		if(length_in_ms)
			*length_in_ms=(locDSD->SampleRate>0 ? int(locDSD->Samples*1000/locDSD->SampleRate) : -1);
		if(title)
			wcscpy(title,L"Future_File");
#ifdef _DEBUG
		if(debugfile){fprintf(debugfile,"File info:%llu / %i\n",locDSD->Samples,locDSD->SampleRate);fflush(debugfile);}
#endif
		//locDSD->finish();
		delete locDSD;
#ifdef _DEBUG
		if(debugfile){fprintf(debugfile,"Local DSD finished\n");fflush(debugfile);}
#endif
	}
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Finish file info\n");fflush(debugfile);}
#endif
	return;
}

/*int infoDlg(const in_char *fn, HWND hwnd)
{
	// CHANGEME! Write your own info dialog code here
	return INFOBOX_UNCHANGED;
}*/

/*int isourfile(const in_char *fn) {
// return !strncmp(fn,"http://",7); to detect HTTP streams, etc
	return 0;
}*/

//-------------------------------------------------------------------------- MAIN DECODER
// render 576 samples into buf.
// this function is only used by DecodeThread.

// note that if you adjust the size of sample_buffer, for say, 1024
// sample blocks, it will still work, but some of the visualization
// might not look as good as it could. Stick with 576 sample blocks
// if you can, and have an additional auxiliary (overflow) buffer if
// necessary..
int get_576_samples(tDSD_decoder* DSD_decoder, int** decoded_data,
					unsigned char **encoded_data, char *buf){
	//int halfsize=576*DSD.Channels*BPS/8;
	__int64 l=0;
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Start DSD.GetSamples\n");fflush(debugfile);}
#endif

	l=DSD.get_samples(576*DSD_decoder->x_factor,encoded_data);//l=bytes in one channel
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Start DSD_decodr.decode_block\n");fflush(debugfile);}
#endif

	//DSD_decoder.dummy_block(encoded_data,decoded_data);
	DSD_decoder->decode_block(encoded_data,decoded_data);
	//l=l*Channels*(BPS/8)/DSD_decoder.x_factor/8;

	//int newsize=(l*8/DSD_decoder.x_factor)*DSD_decoder.channels*(BPS/8);//Exact value
	//int newsize=(l==0)?0:576*DSD_decoder.channels*(BPS/8);//zero or 576 samples
	int newsize=(l==576*DSD_decoder->x_factor/8)?576*DSD_decoder->channels*(BPS/8):0;//zero or 576 samples
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Start sound2char\n");fflush(debugfile);}
#endif

	int buk=0;
	for(int sm=0;sm<576;sm++){
		for(int ch=0;ch<DSD.Channels;ch++){
			/*for(int bte=0;bte<BPS;bte+=8){
				buf[buk++]=(decoded_data[ch][sm]>>bte)&0xff;
			}/*/
			memcpy(&buf[buk], &decoded_data[ch][sm], 3);
			buk += 3;/**/
		}
	}

#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Finish 576-block\n");fflush(debugfile);}
#endif
	return (newsize);
}

DWORD WINAPI DecodeThread(LPVOID b)
{
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Start DecodeThread\n");fflush(debugfile);}
#endif
	tDSD_decoder* DSD_decoder = (tDSD_decoder*)b;
	if (DSD_decoder)
	{
		char* sample_data = new char[DSD.Channels * 576 * 8 * (BPS / 8)];//output
		int done = 0; // set to TRUE if decoding has finished
		__int64 decode_pos_samples = 0;//current decoding position in samples (44100)

		int** decoded_data = new int* [DSD.Channels];
		if (decoded_data) {
			for (int ch = 0; ch < DSD.Channels; ch++) {
				decoded_data[ch] = new int[576];
			}
		}

		const int encoded_size = 576 * DSD_decoder->x_factor / 8;
		unsigned char** encoded_data = new unsigned char* [DSD.Channels];
		if (encoded_data) {
			for (int ch = 0; ch < DSD.Channels; ch++) {
				encoded_data[ch] = new unsigned char[encoded_size];
			}
		}

		while (!killDecodeThread)
		{
			if (seek_needed != -1) { // seek is needed.
				decode_pos_ms = seek_needed;

				decode_pos_samples = decode_pos_ms;
				decode_pos_samples = decode_pos_samples * SAMPLERATE / 1000;

				__int64 offset = seek_needed;
				offset = offset * DSD.SampleRate / 1000;

				seek_needed = -1;
				//if(debugfile){fprintf(debugfile,"Start rewind %i / %i\n",seek_needed,offset);fflush(debugfile);}
				DSD.rewindto(offset);
				//if(debugfile){fprintf(debugfile,"Finish rewind %i / %i\n",seek_needed,offset);fflush(debugfile);}
			}

			if (done) { // done was set to TRUE during decoding, signaling eof
#ifdef _DEBUG
				if (debugfile) { fprintf(debugfile, "Done...\n"); fflush(debugfile); }
#endif

				plugin.outMod->CanWrite();		// some output drivers need CanWrite
											// to be called on a regular basis.

				if (!plugin.outMod->IsPlaying())
				{
					// we're done playing, so tell Winamp and quit the thread.
					/*PostMessage(plugin.hMainWindow,WM_WA_MPEG_EOF,0,0);/*/
					PostEOF();/**/
					break;//return 0;	// quit thread
				}
				Sleep(10);		// give a little CPU time back to the system.
			}
			else if (paused) {
				Sleep(10);
			}
			else {
				int bl = ((576 * DSD.Channels * (BPS / 8)) * (plugin.dsp_isactive() ? 2 : 1));

				// CanWrite() returns the number of bytes you can write, so we check that
				// to the block size. the reason we multiply the block size by two if
				// plugin.dsp_isactive() is that DSP plug-ins can change it by up to a
				// factor of two (for tempo adjustment).
				if (plugin.outMod->CanWrite() >= bl) {
#ifdef _DEBUG
					if (debugfile) { fprintf(debugfile, "Start get576samples %i\n", bl); fflush(debugfile); }
#endif
					int samples = get_576_samples(DSD_decoder, decoded_data, encoded_data, sample_data);	   // retrieve samples
#ifdef _DEBUG
					if (debugfile) { fprintf(debugfile, "Finish get576samples %i\n", l); fflush(debugfile); }
#endif
					if (!samples) {			// no samples means we're at eof
						done = 1;
					}
					else {	// we got samples!
					   // give the samples to the vis subsystems
#ifdef _DEBUG
						if (debugfile) { fprintf(debugfile, "SA Add pcm data %i\n", decode_pos_ms); fflush(debugfile); }
#endif
						plugin.SAAddPCMData((char*)sample_data, DSD.Channels, BPS, decode_pos_ms);
#ifdef _DEBUG
						if (debugfile) { fprintf(debugfile, "VS Add pcm data %i\n", decode_pos_ms); fflush(debugfile); }
#endif
						/*plugin.VSAAddPCMData((char*)sample_data, DSD.Channels, BPS, decode_pos_ms);*/
						// adjust decode position variable
						decode_pos_samples += 576; //(576*1000)/SAMPLERATE;
						decode_pos_ms = int((decode_pos_samples * 1000) / SAMPLERATE);
#ifdef _DEBUG
						if (debugfile) { fprintf(debugfile, "Finish Add pcm data %i\n", decode_pos_ms); fflush(debugfile); }
#endif
						// if we have a DSP plug-in, then call it on our samples
						if (plugin.dsp_isactive()) {
							samples = plugin.dsp_dosamples((short*)sample_data, samples,
														   BPS, DSD.Channels, SAMPLERATE);
						}

						// write the pcm data to the output system
						plugin.outMod->Write(sample_data, samples);
					}
				}
				else Sleep(20);
				// if we can't write data, wait a little bit. Otherwise, continue
				// through the loop writing more data (without sleeping)
			}
		}
		if (DSD.f) { fclose(DSD.f); DSD.f = 0; }//for next opening...
#ifdef _DEBUG
		if (debugfile) { fprintf(debugfile, "Finish DecodeThread\n"); fflush(debugfile); }
#endif

		//--- Free buffer
		if (encoded_data) {
			for (int ch = 0; ch < DSD.Channels; ch++) {
				if (encoded_data[ch]) {
					delete[] encoded_data[ch];
					encoded_data[ch] = 0;
				}
			}

			delete[] encoded_data;
			encoded_data = 0;
		}

		if (decoded_data) {
			for (int ch = 0; ch < DSD.Channels; ch++) {
				if (decoded_data[ch]) {
					delete[] decoded_data[ch];
					decoded_data[ch] = 0;
				}
			}

			delete[] decoded_data;
			decoded_data = 0;
		}

		if (sample_data) {
			delete[]sample_data;
			sample_data = 0;
		}

		delete DSD_decoder;
	}

	if (thread_handle != NULL)
	{
		CloseHandle(thread_handle);
		thread_handle = NULL;
	}
	return 0;
}

// called when winamp wants to play a file
int play(const in_char *fn){
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Start Playing\n");fflush(debugfile);}
#endif

	paused=0;
	decode_pos_ms=0;
	seek_needed=-1;

	tDSD_decoder* DSD_decoder = new tDSD_decoder();
	if (!DSD_decoder)return 1;

	// CHANGEME! Write your own file opening code here
	//f=fopen(fn,"rb");
	FILE *f = _wfsopen(fn, L"rb", _SH_DENYNO);
	if(f==0)return 1;

	//------------------------ New DSD+Decoder
	DSD.start(f);
	//int NCH=DSD.Channels;//Channels
	if(DSD.SampleRate==0) return 1;//Zero samplerate :)
	else if((DSD.SampleRate%44100)==0){SAMPLERATE=44100;DSD_decoder->set_ch_x(DSD.Channels,DSD.SampleRate/44100);}
	else if((DSD.SampleRate%48000)==0){SAMPLERATE=48000;DSD_decoder->set_ch_x(DSD.Channels,DSD.SampleRate/48000);}
	else return 1;//
	DSD_decoder->set_LSB_MSB(DSD.LSB_first,DSD.MSB_first);

	//------------------------ Get memory
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Getting memory\n");fflush(debugfile);}
#endif

#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Finish Getting memory\n");fflush(debugfile);}
#endif
	//------------------------ Main part


	//strcpy(lastfn,fn); //???

	// -1 and -1 are to specify buffer and prebuffer lengths.
	// -1 means to use the default, which all input plug-ins should
	// really do.
	const int maxlatency = (plugin.outMod && plugin.outMod->Open && SAMPLERATE && DSD.Channels ?
							plugin.outMod->Open(SAMPLERATE, DSD.Channels, BPS, -1, -1) : -1);

	// maxlatency is the maxium latency between a outMod->Write() call and
	// when you hear those samples. In ms. Used primarily by the visualization
	// system.
	if (maxlatency < 0){
		fclose(f);f=0;
		return 1;
	}

	// dividing by 1000 for the first parameter of setinfo makes it
	// display 'H'... for hundred.. i.e. 14H Kbps.
	plugin.SetInfo((DSD.SampleRate*DSD.Channels)/1000,SAMPLERATE/1000,DSD.Channels,1);

	// initialize visualization stuff
	plugin.SAVSAInit(maxlatency,SAMPLERATE);
	plugin.VSASetInfo(SAMPLERATE,DSD.Channels);

	// set the output plug-ins default volume.
	// volume is 0-255, -666 is a token for
	// current volume.
	plugin.outMod->SetVolume(-666);

	decode_pos_ms=0;
	// launch decode thread
	killDecodeThread=0;
	thread_handle = StartThread(DecodeThread, DSD_decoder, static_cast<int>(plugin.config->
								GetInt(playbackConfigGroupGUID, L"priority",
								THREAD_PRIORITY_HIGHEST)), 0, NULL);
	// set the thread priority
	if (thread_handle == NULL)
	{
		fclose(f);f=0;
		return -1;
	}
	return 0;
}
void pause() { paused=1; if (plugin.outMod) plugin.outMod->Pause(1); }
void unpause() { paused=0; if (plugin.outMod) plugin.outMod->Pause(0); }
int ispaused() { return paused; }
void stop() {
	if (thread_handle != NULL)
	{
		killDecodeThread=1;
		if (WaitForSingleObject(thread_handle,10000) == WAIT_TIMEOUT)
		{
			/*MessageBoxW(plugin.hMainWindow,L"error asking thread to die!\n",
				L"error killing decode thread",0);
			TerminateThread(thread_handle,0);*/
		}
		CloseHandle(thread_handle);
		thread_handle = NULL;
	}

	// close output system
	if (plugin.outMod && plugin.outMod->Close)
	{
		plugin.outMod->Close();
	}

	// deinitialize visualization
	if (plugin.outMod)
	{
		plugin.SAVSADeInit();
	}

	// CHANGEME! Write your own file closing code here
	//if (f){fclose(f);f=0;}

	//--- Free buffer
	//if((sample_buffer_size!=0)&&(sample_buffer!=0))
	//delete[] sample_buffer;
	//sample_buffer_size=0;
	//sample_buffer=0;

}

//-------------------------------------------------------------------------- sizes
// returns length of playing track (in ms)
int getlength() {
	return int(DSD.Samples*1000/DSD.SampleRate);
}


// returns current output position, in ms.
// you could just use return plugin.outMod->GetOutputTime(),
// but the dsp plug-ins that do tempo changing tend to make
// that wrong.
int getoutputtime() {
	return (plugin.outMod ? decode_pos_ms+(plugin.outMod->GetOutputTime()-
									plugin.outMod->GetWrittenTime()) : 0);
}


// called when the user releases the seek scroll bar.
// usually we use it to set seek_needed to the seek
// point (seek_needed is -1 when no seek is needed)
// and the decode thread checks seek_needed.
void setoutputtime(int time_in_ms) {
	seek_needed=time_in_ms;
}

//-------------------------------------------------------------------------- sound processing
void setvolume(int volume) {
#ifdef _DEBUG
	if(debugfile){fprintf(debugfile,"Set Volume %i\n",volume);fflush(debugfile);}
#endif
	if (plugin.outMod && plugin.outMod->SetVolume)
	{
		plugin.outMod->SetVolume(volume);
	}
	//DSD_decoder.set_volume(volume);
	//plugin.outMod->SetVolume(255);
}

void setpan(int pan) {
	if (plugin.outMod && plugin.outMod->SetPan)
	{
		plugin.outMod->SetPan(pan);
	}
}


// exported symbol. Returns output module.

// should return a child window of 513x271 pixels (341x164 in msvc dlg units), or return NULL for no tab.
// Fill in name (a buffer of namelen characters), this is the title of the tab (defaults to "Advanced").
// filename will be valid for the life of your window. n is the tab number. This function will first be 
// called with n == 0, then n == 1 and so on until you return NULL (so you can add as many tabs as you like).
// The window you return will recieve WM_COMMAND, IDOK/IDCANCEL messages when the user clicks OK or Cancel.
// when the user edits a field which is duplicated in another pane, do a SendMessage(GetParent(hwnd),WM_USER,(WPARAM)L"fieldname",(LPARAM)L"newvalue");
// this will be broadcast to all panes (including yours) as a WM_USER.
extern "C" __declspec(dllexport) HWND winampAddUnifiedFileInfoPane(int n, const wchar_t* filename, HWND parent, wchar_t* name, size_t namelen)
{
	return NULL;
}

extern "C" __declspec(dllexport) int winampGetExtendedFileInfoW(const wchar_t* filename, const char* metadata, wchar_t* dest, int destlen)
{
	if (SameStrA(metadata, "type") ||
		SameStrA(metadata, "streammetadata"))
	{
		dest[0] = L'0';
		dest[1] = L'\0';
		return 1;
	}
	else if ((SameStrNA(metadata, "stream", 6) &&
			  (SameStrA((metadata + 6), "type") ||
			   SameStrA((metadata + 6), "genre") ||
			   SameStrA((metadata + 6), "url") ||
			   SameStrA((metadata + 6), "name") ||
			   SameStrA((metadata + 6), "title"))) ||
			 SameStrA(metadata, "reset"))
	{
		return 0;
	}
	else if (SameStrA(metadata, "family"))
	{
		LPCWSTR e = FindPathExtension(filename);
		if (e != NULL)
		{
			// TODO localise
			//int pID = -1;
			if (SameStr(e, L"DSF"))
			{
				/*pID = IDS_FAMILY_STRING_DSF;/*/
				CopyCchStr(dest, destlen, L"Sony Direct Stream Digital File Format");/**/
			}
			else if (SameStr(e, L"DFF"))
			{
				/*pID = IDS_FAMILY_STRING_DFF;/*/
				CopyCchStr(dest, destlen, L"Phillips Direct Stream Digital File Format");/**/
			}
			else
			{
				return 0;
			}

			/*if (pID != -1)
			{
				LngStringCopy(pID, dest, destlen);
				return 1;
			}*/
			return 1;
		}
		return 0;
	}

	if (!filename || !*filename)
	{
		return 0;
	}

	return 0;
}

// return 1 if you want winamp to show it's own file info dialogue, 0 if you want to show your own (via In_Module.InfoBox)
// if returning 1, remember to implement winampGetExtendedFileInfo("formatinformation")!
extern "C" __declspec(dllexport) int winampUseUnifiedFileInfoDlg(const wchar_t* fn)
{
	return 1;
}

extern "C" __declspec(dllexport) In_Module * winampGetInModule2()
{
	return &plugin;
}

extern "C" __declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param)
{
	// prompt to remove our settings with default as no (just incase)
	/*if (UninstallSettingsPrompt(reinterpret_cast<const wchar_t*>(plugin.description)))
	{
		SaveNativeIniString(PLUGIN_INI, _T("APE Plugin Settings"), 0, 0);
	}*/

	// as we're not hooking anything and have no settings we can support an on-the-fly uninstall action
	return IN_PLUGIN_UNINSTALL_NOW;
}

struct ExtendedRead
{
	ExtendedRead() : f(NULL), encoded_data(NULL), decoded_data(NULL)
	{
	}

	~ExtendedRead()
	{
		if (f != NULL)
		{
			fclose(f);
			f = NULL;
		}
	}

	FILE* f;
	tDSD parser;
	tDSD_decoder decoder;
	unsigned char** encoded_data;
	int** decoded_data;
};

extern "C" __declspec(dllexport) intptr_t winampGetExtendedRead_openW(const wchar_t* fn, int* size, int* bps, int* nch, int* srate)
{
	ExtendedRead* e = new ExtendedRead();
	if (e)
	{
		e->f = _wfsopen(fn, L"rb", _SH_DENYNO);
		if (e->f != 0)
		{
			e->parser.start(e->f);

			if (e->parser.SampleRate != 0)
			{
				if ((e->parser.SampleRate % 44100) == 0)
				{
					*srate = 44100;
					e->decoder.set_ch_x(e->parser.Channels, e->parser.SampleRate / 44100);
				}
				else if ((e->parser.SampleRate % 48000) == 0)
				{
					*srate = 48000;
					e->decoder.set_ch_x(e->parser.Channels, e->parser.SampleRate / 48000);
				}
				else
				{
					goto fail;
				}

				e->decoder.set_LSB_MSB(e->parser.LSB_first, e->parser.MSB_first);

				const int encoded_size = (576 * e->decoder.x_factor / 8);
				e->encoded_data = new unsigned char* [e->parser.Channels];

				for (int ch = 0; ch < e->parser.Channels; ch++)
				{
					e->encoded_data[ch] = new unsigned char[encoded_size];
				}

				e->decoded_data = new int* [e->parser.Channels];

				for (int ch = 0; ch < e->parser.Channels; ch++)
				{
					e->decoded_data[ch] = new int[576];
				}

				*bps = BPS;
				*nch = e->parser.Channels;
				*size = /*-1/*/(int)(e->parser.Samples * (BPS / 8) * e->parser.Channels)/**/;
				return (intptr_t)e;
			}

fail:
			fclose(e->f);
		}

		SafeFree(e);
	}
	return 0;
}

extern "C" __declspec(dllexport) intptr_t winampGetExtendedRead_getData(intptr_t handle, char* dest, size_t len, int* /*killswitch*/)
{
	ExtendedRead* e = reinterpret_cast<ExtendedRead*>(handle);
	if (e)
	{
		__int64 l = 0;
#ifdef _DEBUG
		if (debugfile) { fprintf(debugfile, "Start DSD.GetSamples\n"); fflush(debugfile); }
#endif

		l = e->parser.get_samples(576 * e->decoder.x_factor, e->encoded_data);//l=bytes in one channel
#ifdef _DEBUG
		if (debugfile) { fprintf(debugfile, "Start DSD_decodr.decode_block\n"); fflush(debugfile); }
#endif

		e->decoder.decode_block(e->encoded_data, e->decoded_data);

		const size_t newsize = (l == 576 * e->decoder.x_factor / 8) ? 576 * e->decoder.channels * (BPS / 8) : 0;//zero or 576 samples
#ifdef _DEBUG
		if (debugfile) { fprintf(debugfile, "Start sound2char\n"); fflush(debugfile); }
#endif
		if (newsize > 0)
		{
			int buk = 0;
			for (int sm = 0; sm < 576; sm++) {
				for (int ch = 0; ch < e->parser.Channels; ch++) {
					/*for (int bte = 0; bte < BPS; bte += 8) {
						dest[buk++] = ((e->decoded_data[ch][sm] >> bte) & 0xff);
					}/*/
					memcpy(&dest[buk], &e->decoded_data[ch][sm], 3);
					buk += 3;/**/
				}
			}

#ifdef _DEBUG
			if (debugfile) { fprintf(debugfile, "Finish 576-block\n"); fflush(debugfile); }
#endif
		}

		return min(len, newsize);
	}
	return 0;
}

extern "C" __declspec(dllexport) int winampGetExtendedRead_setTime(intptr_t handle, int time_in_ms)
{
	ExtendedRead* e = reinterpret_cast<ExtendedRead*>(handle);
	return (e ? !e->parser.rewindto(((time_in_ms * e->parser.SampleRate) / 1000ULL)) : 0);
}

extern "C" __declspec(dllexport) void winampGetExtendedRead_close(intptr_t handle)
{
	ExtendedRead* e = reinterpret_cast<ExtendedRead*>(handle);
	if (e)
	{
		delete e;
	}
}