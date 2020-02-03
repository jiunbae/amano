// LPR-visualize.cpp : Defines the entry point for the application.
//
#define _CRT_SECURE_NO_WARNINGS

#include "resource.h"
#include "framework.h"
#include "detector.h"

#include "acognitive_post_def.h"

#include "lib/warp.hpp"

#include <iostream>
#include <fstream>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

//
//   FUNCTION: GUI utility
//
MNHandle* g_Detector = nullptr;
std::vector<MNBoxT>* results = nullptr;
std::vector<MNBoxT>* Yresults = nullptr;


cv::Size shape_raw{ 1536, 2048 };
cv::Size shape_window{ 1536, 1024 + 60 };
cv::Size shape_image{ 768,1024 };
cv::Size shape_ground{ 768, 1024 };

char szFileName[100] = { 0, };

Gdiplus::Bitmap* image_map = NULL, * ground_map = NULL;
cv::Mat* image = NULL, * ground = NULL;
cv::Mat* image_resized = NULL, * ground_resized = NULL;
cv::Mat* crop = NULL;
cv::Mat image_show;

Calibration* calib = NULL;
Warp* warp = NULL;

MNHandle* g_Classifier = nullptr;
BOOL	g_ParkingEvent = FALSE;
int		gX = 0, gY = 0;
std::vector<std::tuple<float, float ,float, float>> g_Parkings;
std::vector<int> g_ParkingResults;

AP_HANDLE	g_ApHandle = NULL;
char	g_ACognitiveUrl[256] = {0};
BOOL	g_UnderFlip = FALSE;
HWND	g_hWnd = NULL;


void resizeImage(int width, int height) {
	shape_raw.width = width;
	shape_raw.height = height;
}
void resizeWindow(int width, int height) {
	float ratio = shape_raw.height / (float)shape_raw.width;
	height = (int)(width * ratio / 2.f);
	width = height / ratio * 2.f;

	int image_width = (int)(width / 2.f);
	if ((image_width * 3) % 4) {
		image_width += ((image_width * 3) % 4);
	}
	shape_image.width = image_width;
	shape_image.height = height;

	int ground_width = (int)(height * (1920.f / 1080.f));
	if ((ground_width * 3) % 4) {
		ground_width += ((ground_width * 3) % 4);
	}
	shape_ground.width = ground_width;
	shape_ground.height = height;


	shape_window.width = image_width + ground_width;
	shape_window.height = height + 60;


//	calib = new Calibration(shape_raw);
//	warp = new Warp(*calib, shape_window);
}

BOOL openFile(function<void(const OPENFILENAMEA&)> callback, const char* filter = nullptr) {
	OPENFILENAMEA openFileDialog;

	strcpy(szFileName, "");

	if (filter == nullptr) {
		filter = "Image (*.jpg)|*.jpg|*.png";
	}

	ZeroMemory(&openFileDialog, sizeof(openFileDialog));
	openFileDialog.lStructSize = sizeof(openFileDialog);
	openFileDialog.hwndOwner = NULL;
	openFileDialog.lpstrFilter = filter;
	openFileDialog.lpstrFile = szFileName;
	openFileDialog.nMaxFile = MAX_PATH;
	openFileDialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	if (GetOpenFileNameA(&openFileDialog)) {
		struct stat buffer;
		if (stat(openFileDialog.lpstrFile, &buffer) == 0) {
			callback(openFileDialog);
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

void saveFile(function<void(const OPENFILENAMEA&)> callback, const char* filter = nullptr) {
	OPENFILENAMEA openFileDialog;

	// DEBUG
	// strcpy(szFileName, "calib.txt");
	int len = strlen(szFileName);
	szFileName[len - 3] = 't';
	szFileName[len - 2] = 'x';
	szFileName[len - 1] = 't';


	if (filter == nullptr) {
		filter = "Calibration Setting (*.txt)|*.txt";
	}

	ZeroMemory(&openFileDialog, sizeof(openFileDialog));
	openFileDialog.lStructSize = sizeof(openFileDialog);
	openFileDialog.hwndOwner = NULL;
	openFileDialog.lpstrFilter = filter;
	openFileDialog.lpstrFile = szFileName;
	openFileDialog.nMaxFile = MAX_PATH;
	openFileDialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	if (GetSaveFileNameA(&openFileDialog)) {
		callback(openFileDialog);
	}
}

void indicator(HWND hWnd) {
	wstring buffer;
	wstringstream wss;

	HMENU menu = GetMenu(hWnd);

	wss.str(wstring());
	wss << "Center: (" << calib->cx << ", " << calib->cy << ")";
	ModifyMenu(menu, ID_CENTER, MF_BYCOMMAND | MF_STRING, ID_CENTER, wss.str().c_str());

	wss.str(wstring());
	wss << "F: (" << calib->fx << ", " << calib->fy << ")";
	ModifyMenu(menu, ID_F, MF_BYCOMMAND | MF_STRING, ID_F, wss.str().c_str());

	wss.str(wstring());
	wss << "CV: (" << setprecision(2) << warp->center.val[0] << ", " << warp->center.val[1] << ", " << warp->center.val[2] << ")";
	ModifyMenu(menu, ID_CV, MF_BYCOMMAND | MF_STRING, ID_CV, wss.str().c_str());

	wss.str(wstring());
	wss << "UV: (" << setprecision(2) << warp->up.val[0] << ", " << warp->up.val[1] << ", " << warp->up.val[2] << ")";
	ModifyMenu(menu, ID_UV, MF_BYCOMMAND | MF_STRING, ID_UV, wss.str().c_str());

	wss.str(wstring());
	wss << "RV: (" << setprecision(2) << warp->right.val[0] << ", " << warp->right.val[1] << ", " << warp->right.val[2] << ")";
	ModifyMenu(menu, ID_RV, MF_BYCOMMAND | MF_STRING, ID_RV, wss.str().c_str());

	DrawMenuBar(hWnd);
}

void redraw(HWND hWnd, bool inference = false) {
	warp->Map(*image, ground);

	image->copyTo(*image_resized);
	warp->DrawBoundingBox(image_resized);

	cv::resize(*image_resized, *image_resized, shape_image);
	cv::resize(*ground, *ground_resized, shape_ground);

	if (g_UnderFlip) {
		cv::imwrite("pimg_0.jpg", *ground_resized);
		cv::Mat tUnder = (*ground_resized)(cv::Rect(0, ground_resized->rows / 2, ground_resized->cols, ground_resized->rows / 2));
		cv::rotate(tUnder, tUnder, ROTATE_180);
		cv::imwrite("pimg_1.jpg", *ground_resized);
	}

	if (inference && g_Detector) {
		results = MN_Detect(g_Detector, *ground_resized);
	}

	image_map = new Gdiplus::Bitmap(
		shape_image.width, shape_image.height, image_resized->step1(),
		PixelFormat24bppRGB, image_resized->data);
	
	ground_resized->copyTo(image_show);
	if (results != nullptr) {
		for (auto box : *results) {
			if (box.prob > .3f) {
				cv::rectangle(image_show, {
					static_cast<int>(box.x), static_cast<int>(box.y),
					static_cast<int>(box.w), static_cast<int>(box.h),
				}, cv::Scalar(255, 0, 0), 2);
			}
		}
	}

	if (Yresults) {
		for (auto box : *Yresults) {
			cv::rectangle(image_show, {
				static_cast<int>(box.x), static_cast<int>(box.y),
				static_cast<int>(box.w), static_cast<int>(box.h),
			}, cv::Scalar(0, 0, 255), 2);
		}

		cv::imwrite("pimg_2.jpg", image_show);
	}

	for (int i = 0; i < g_Parkings.size(); i++) {
		cv::Scalar color;
		float x, y, w, h;
		std::tie(x, y, w, h) = g_Parkings[i];

		if (g_ParkingResults.size() > i) {
			color = g_ParkingResults[i] > .5f ? cv::Scalar(255, 0, 0) : cv::Scalar(0, 0, 255);
		} else {
			color = cv::Scalar(0, 255, 0);
		}

		cv::rectangle(image_show, {
			static_cast<int>(x * shape_image.width), static_cast<int>(y * shape_image.width),
		}, {
			static_cast<int>(w* shape_image.width), static_cast<int>(h* shape_image.width),
		}, color, 2);
	}

	ground_map = new Gdiplus::Bitmap(
		shape_ground.width, shape_ground.height, image_show.step1(),
		PixelFormat24bppRGB, image_show.data);
	

	InvalidateRect(hWnd, NULL, NULL);
}
//
//   FUNCTION: GUI utility
//


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
	
	// Initialize gdiplus
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_DETECTOR, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
	
	// Initialize calibration parameter
	image_resized = new cv::Mat();
	ground_resized = new cv::Mat();

	if (!openFile([&](const OPENFILENAMEA& openFileDialog) {
		image = new cv::Mat(cv::imread(string(openFileDialog.lpstrFile), cv::IMREAD_COLOR));
		ground = new cv::Mat();

		resizeImage(image->cols, image->rows);
		resizeWindow(image->cols, image->rows);

		calib = new Calibration(shape_raw);
		warp = new Warp(*calib, shape_window);

		// Load calib config sequentialy
		int len = strlen(openFileDialog.lpstrFile);
		openFileDialog.lpstrFile[len - 3] = 't';
		openFileDialog.lpstrFile[len - 2] = 'x';
		openFileDialog.lpstrFile[len - 1] = 't';
		std::ifstream in(openFileDialog.lpstrFile);

		if (in.is_open()) {
			in >> calib->cx >> calib->cy >> calib->fx >> calib->fy;

			in >> warp->center[0] >> warp->center[1] >> warp->center[2];
			in >> warp->up[0] >> warp->up[1] >> warp->up[2];
			in >> warp->right[0] >> warp->right[1] >> warp->right[2];
			in.close();

			warp->Update();
		}
	})) {
		return FALSE;;
	}

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow)) {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DETECTOR));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
	
	// Destroy gdiplus
	Gdiplus::GdiplusShutdown(gdiplusToken);

    return (int) msg.wParam;
}


//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DETECTOR));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DETECTOR);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
	   CW_USEDEFAULT, 0, (int)shape_window.width, (int)shape_window.height,
	   nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

bool adjust_flag = false;

long _stdcall RequestImageCallBack(TRequestImage* AImageResult, WPARAM wParam)
{
	int tSize = AImageResult->m_ResultCount;

	if (tSize > 0 && !Yresults)
		Yresults = new std::vector<MNBoxT>;
	if (Yresults) Yresults->clear();

	for (int i = 0; i < tSize; i++)
	{
		TBbox &tBbox = AImageResult->m_Result[i];

		if (Yresults)
		{
			MNBoxT tPushBox;
			tPushBox.x = tBbox.x;
			tPushBox.y = tBbox.y;
			tPushBox.w = tBbox.w;
			tPushBox.h = tBbox.h;
			Yresults->push_back(tPushBox);
		}

	}

	return 0;
}

// 0: ����ϳݸ�
// 1: Y������
// 2: �Ѵ�
void CarDetect(int AType)
{
	// acogurl.txt
	FILE* tf = fopen("acogurl.txt", "rt");
	if (tf)
	{
		fread(g_ACognitiveUrl, 1, sizeof(g_ACognitiveUrl), tf);
		fclose(tf);
		tf = NULL;
	}

	

	if (AType == 1 || AType == 2)
	{
		if (strlen(g_ACognitiveUrl) <= 0)
		{
			::MessageBoxA(g_hWnd, "acogurl.txt�� Y����URL�� �����ϴ�. Ȯ�κ�Ź�帳�ϴ�.", "Ȯ��", MB_OK);
			return;
		}

		if (ground_resized)
		{
			/////////////////////////////////////////////
			std::vector<int> compression_params;
			int tJpgCompression = 90;
			// For JPEG, it can be a quality ( CV_IMWRITE_JPEG_QUALITY ) from 0 to 100 (the higher is the better). Default value is 95.
			compression_params.push_back(1);// CV_IMWRITE_JPEG_QUALITY);
			compression_params.push_back(tJpgCompression);
			// ���� �̹���ó�� Acognitive ��û.
			vector<uchar> buf;
			cv::imencode(".jpg", *ground_resized, buf, compression_params);

			ap_Request_ImageBuf(g_ApHandle, g_ACognitiveUrl, buf.data(), buf.size(), "detector_test", FALSE);
		}
	}

	if (AType == 0 || AType == 2)
	{
		redraw(g_hWnd, true);
	}
	
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	g_hWnd = hWnd;

    switch (message)
    {
	case WM_CREATE:
		{
			g_ApHandle = ap_Init(RequestImageCallBack, (WPARAM)0);

			if (g_Detector != nullptr) {
				MN_Dispose(g_Detector);
			}

			g_Detector = MN_Init("./bin/amano-script.pt", 0);

			// acogurl.txt
			FILE* tf = fopen("acogurl.txt", "rt");
			if (tf)
			{
				fread(g_ACognitiveUrl, 1, sizeof(g_ACognitiveUrl), tf);
				fclose(tf);
				tf = NULL;
			}

		//	redraw(hWnd);
		}	
		break;

	case WM_SIZE:
		{
			int nWidth = LOWORD(lParam);
			int nHeight = HIWORD(lParam);

			if (!adjust_flag) {
				adjust_flag = true;
				resizeWindow(nWidth, nHeight);
				SetWindowPos(hWnd, NULL, 0, 0, shape_window.width, shape_window.height,
					SWP_NOSENDCHANGING | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
				redraw(hWnd);
			} else {
				adjust_flag = false;
			}
		}
		break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
			case ID_DETECT_LOAD:
				openFile([&](const OPENFILENAMEA& openFileDialog) {
					if (g_Detector != nullptr) {
						MN_Dispose(g_Detector);
					}

					g_Detector = MN_Init(openFileDialog.lpstrFile, 0);
				});
				break;
			case ID_DETECT_INFERENCE:
				{
					if (g_Detector == nullptr)
						break;

					if (results != nullptr)
						delete results;

					redraw(hWnd, true);
				}
				break;
			case ID_DETECT_SAVE:
				saveFile([&](const OPENFILENAMEA& saveFileDialog) {
					std::ofstream out(saveFileDialog.lpstrFile);
					if (out.is_open() && results != nullptr) {
						for (auto box : *results) {
							out << std::fixed << box.prob
								<< box.x << " " << box.y << " "
								<< box.w << " " << box.h << std::endl;
						}
					}
					out.close();
				}, "Bounding boxes (.txt)|*.txt");
				break;
			case ID_FILE_OPEN:
				openFile([&](const OPENFILENAMEA& openFileDialog) {
					image = new cv::Mat(cv::imread(string(openFileDialog.lpstrFile), cv::IMREAD_COLOR));
					ground = new cv::Mat();

					resizeImage(image->cols, image->rows);
					resizeWindow(image->cols, image->rows);
					SetWindowPos(hWnd, NULL, 0, 0, shape_window.width, shape_window.height, SWP_NOMOVE | SWP_NOREPOSITION);
					UpdateWindow(hWnd);
					// Load calib config sequentialy
					int len = strlen(openFileDialog.lpstrFile);
					openFileDialog.lpstrFile[len - 3] = 't';
					openFileDialog.lpstrFile[len - 2] = 'x';
					openFileDialog.lpstrFile[len - 1] = 't';
					std::ifstream in(openFileDialog.lpstrFile);

					if (in.is_open()) {
						in >> calib->cx >> calib->cy >> calib->fx >> calib->fy;

						in >> warp->center[0] >> warp->center[1] >> warp->center[2];
						in >> warp->up[0] >> warp->up[1] >> warp->up[2];
						in >> warp->right[0] >> warp->right[1] >> warp->right[2];
						in.close();

						warp->Update();
						indicator(hWnd);
					}


					redraw(hWnd);
				});
				break;
			case ID_FILE_LOAD:
				openFile([&](const OPENFILENAMEA& openFileDialog) {
					{
						std::ifstream in(openFileDialog.lpstrFile);

						if (in.is_open()) {
							in >> calib->cx >> calib->cy >> calib->fx >> calib->fy;

							in >> warp->center[0] >> warp->center[1] >> warp->center[2];
							in >> warp->up[0] >> warp->up[1] >> warp->up[2];
							in >> warp->right[0] >> warp->right[1] >> warp->right[2];
						}

						in.close();

						warp->Update();
						indicator(hWnd);
						redraw(hWnd);
					}

				}, "Calibration Setting (*.txt)|*.txt");
				break;
			case ID_FILE_SAVE:
				saveFile([&](const OPENFILENAMEA& saveFileDialog) {
					{
						std::ofstream out(saveFileDialog.lpstrFile);

						if (out.is_open()) {
							out << std::fixed
								<< calib->cx << " " << calib->cy << " "
								<< calib->fx << " " << calib->fy << std::endl;
							out << std::fixed
								<< warp->center[0] << " " << warp->center[1] << " " << warp->center[2] << " " << std::endl;
							out << std::fixed
								<< warp->up[0] << " " << warp->up[1] << " " << warp->up[2] << " " << std::endl;
							out << std::fixed
								<< warp->right[0] << " " << warp->right[1] << " " << warp->right[2] << " " << std::endl;
						}

						out.close();
					}
				}, "Calibration Setting (*.txt)|*.txt");
				break;
			case ID_PARKING_START:
				openFile([&](const OPENFILENAMEA& openFileDialog) {
					if (g_Classifier != nullptr) {
						MN_Dispose(g_Classifier);
					}

					g_Classifier = MN_Init(openFileDialog.lpstrFile, 0);
					MN_SetParam(g_Classifier, 224, 224);
				});
				break;
			case ID_PARKING_DRAW:
				{
					wstring buffer;
					wstringstream wss;

					HMENU menu = GetMenu(hWnd);

					wss.str(wstring());

					if (g_ParkingEvent) {
						wss << "Draw (ready)";
					} else {
						wss << "Drawinig";
					}
					g_ParkingEvent = !g_ParkingEvent;

					ModifyMenu(menu, ID_PARKING_DRAW, MF_BYCOMMAND | MF_STRING,
						ID_PARKING_DRAW, wss.str().c_str());
					DrawMenuBar(hWnd);
				}
				break;
			case ID_PARKING_SAVE:
				saveFile([&](const OPENFILENAMEA& saveFileDialog) {
					std::ofstream out(saveFileDialog.lpstrFile);
					if (out.is_open()) {
						for (auto box : g_Parkings) {
							float x, y, w, h;
							std::tie(x, y, w, h) = box;

							out << std::fixed 
								<< x << " " << y << " " 
								<< w << " " << h << std::endl;
						}
					}
					out.close();
					}, "Parking boxes (.txt)|*.txt");
				break;
			case ID_PARKING_LOAD:
				openFile([&](const OPENFILENAMEA& openFileDialog) {
					std::ifstream in(openFileDialog.lpstrFile);

					if (in.is_open()) {
						float x, y, w, h;
						while (in >> x >> y >> w >> h) {
							g_Parkings.push_back({
								x, y, w, h,
							});
						}
					}

					in.close();
					redraw(hWnd);
				}, "Parking boxes (.txt)|*.txt");
				break;
			case ID_PARKING_INFERENCE:
				if (g_Classifier && g_ParkingResults.size() != g_Parkings.size()) {
					for (int i = 0; i < g_Parkings.size(); i++) {
						float x, y, w, h;
						std::tie(x, y, w, h) = g_Parkings[i];

						if (crop == NULL) {
							crop = new cv::Mat();
						}

						(*ground_resized)({
							static_cast<int>(x * shape_image.width), static_cast<int>(y * shape_image.width),
							static_cast<int>(w * shape_image.width - x * shape_image.width),
							static_cast<int>(h * shape_image.width - y * shape_image.width),
							}).copyTo(*crop);

						if (y * shape_image.width > shape_image.height / 2.f) {
							cv::flip(*crop, *crop, 0);
						}

						g_ParkingResults.push_back(MN_Classify(g_Classifier, *crop));
					}
				}
				redraw(hWnd);
				break;
			case ID_PARKING_CLEAR:
				g_ParkingResults.clear();
				g_Parkings.clear();
				redraw(hWnd);
				break;
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
			HDC hdc;

			hdc = BeginPaint(hWnd, &ps);

			if (image_map != NULL && ground_map != NULL) {
				Gdiplus::Graphics graphics(hdc);
				graphics.Clear(Gdiplus::Color::Transparent);

				Gdiplus::Bitmap* render = image_map->Clone(
					Gdiplus::Rect(0, 0, shape_image.width, shape_image.height), PixelFormat24bppRGB);
				graphics.DrawImage(render, 0, 0);

				render = ground_map->Clone(
					Gdiplus::Rect(0, 0, shape_ground.width, shape_ground.height), PixelFormat24bppRGB);
				graphics.DrawImage(render, shape_image.width, 0);
			}

            EndPaint(hWnd, &ps);
        }
        break;

	case WM_KEYDOWN:
		{
			if (results) delete results; results = NULL;
			if (Yresults) delete Yresults; Yresults = NULL;

			int ctrl_flag = GetAsyncKeyState(VK_CONTROL);
			int shift_flag = GetAsyncKeyState(VK_SHIFT);
			float flag = shift_flag ? .1f : 1.f;

			if (ctrl_flag) {
				switch (wParam) {
					case 'O':
						openFile([&](const OPENFILENAMEA& openFileDialog) {
							image = new cv::Mat(cv::imread(string(openFileDialog.lpstrFile), cv::IMREAD_COLOR));
							ground = new cv::Mat();

							resizeImage(image->cols, image->rows);
							resizeWindow(image->cols, image->rows);
							SetWindowPos(hWnd, NULL, 0, 0, shape_window.width, shape_window.height, SWP_NOMOVE | SWP_NOREPOSITION);
							UpdateWindow(hWnd);
							redraw(hWnd);
						});
						break;
					case 'S': 
						saveFile([&](const OPENFILENAMEA& saveFileDialog) {
							{
								std::ofstream out(saveFileDialog.lpstrFile);

								if (out.is_open()) {
									out << std::fixed
										<< calib->cx << " " << calib->cy << " "
										<< calib->fx << " " << calib->fy << std::endl;
									out << std::fixed
										<< warp->center[0] << " " << warp->center[1] << " " << warp->center[2] << " " << std::endl;
									out << std::fixed
										<< warp->up[0] << " " << warp->up[1] << " " << warp->up[2] << " " << std::endl;
									out << std::fixed
										<< warp->right[0] << " " << warp->right[1] << " " << warp->right[2] << " " << std::endl;
								}

								out.close();
							}
						}, "Calibration Setting (*.txt)|*.txt");
						break;
				}
			} else {
				switch (wParam) {
					// Mode (m)
					case 'M':	warp->plane_mode = (warp->plane_mode + 1) % 3; break;

						// Reset (r)
					case 'R':
						warp->Reset();
						calib->Reset();
						break;

						// Zoom (x,z)
					case 'X':	warp->Zoom(1.f + (.1f * flag)); break;
					case 'Z':	warp->Zoom(1.f - (.1f * flag)); break;

						// Rotate (q,e)
					case 'Q':	warp->Rotate(.1f * flag); break;
					case 'E':	warp->Rotate(-.1f * flag); break;

						// Translate cx, cy (w,a,s,d)
					case 'W':	calib->cy -= 5 * flag; break;
					case 'A':	calib->cx -= 5 * flag; break;
					case 'S':	calib->cy += 5 * flag; break;
					case 'D':	calib->cx += 5 * flag; break;

						// Aspect ratio fy (1,2=f, 3,4=fy)
					case '1':	calib->fx *= (1 + (.05f * flag)); break;
					case '3':	calib->fy *= (1 - (.05f * flag)); break;
					case '2':	calib->fx /= (1 + (.05f * flag)); break;
					case '4':	calib->fy /= (1 - (.05f * flag)); break;

					case '5':
					{
						// ���� ���� ������.
						g_UnderFlip = !g_UnderFlip;

						break;
					}
					// ����ϳݸ�
					case '6':
						CarDetect(0);
						break;
						// Y������
					case '7':
						CarDetect(1);
						break;
						// �Ѵ�
					case '8':
						CarDetect(2);
						break;
					default:
						break;
				}
			}

		}
		break;

	case WM_KEYUP:
		warp->Update();
		indicator(hWnd);
		redraw(hWnd);
		break;

	case WM_LBUTTONDOWN:
		if (g_ParkingEvent) {
			int lX = LOWORD(lParam);
			int lY = HIWORD(lParam);

			if (lX >= shape_image.width) {
				gX = LOWORD(lParam);
				gY = HIWORD(lParam);
			}
		}
		break;

	case WM_LBUTTONUP:
		if (g_ParkingEvent) {
			if (gX && gY) {
				int lX = LOWORD(lParam);
				int lY = HIWORD(lParam);
				
				if (lX >= shape_image.width) {
					g_Parkings.push_back({
						(MIN(gX, lX) - shape_image.width) / static_cast<float>(shape_image.width), 
						MIN(gY, lY) / static_cast<float>(shape_image.width),
						(MAX(gX, lX) - shape_image.width) / static_cast<float>(shape_image.width), 
						MAX(gY, lY) / static_cast<float>(shape_image.width),
					});
					redraw(hWnd);
				}
			}
			gX = gY = 0;
		}
		break;

    case WM_DESTROY:
		DeleteObject(image_map);
		DeleteObject(ground_map);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);

    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}