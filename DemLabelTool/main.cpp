#include <utils.h>

string FILE_LIST_PATH = "/home/gaobiao/Documents/RoadSegmentation_IV2019/data/guilin_for_annotation.txt";
string ANNOTATION_PATH = "/home/gaobiao/Documents/RoadSegmentation_IV2019/data/guilin_annotated/";
string UNANNOTATED_PATH = "/home/gaobiao/Documents/RoadSegmentation_IV2019/data/guilin_for_annotation/";

string CALIB_FILE = "/media/gaobiao/SeagateBackupPlusDrive/201/201-2018/data/guilin/hongling_Round1/vel_hongling.calib";
string DSV_FILE = "/media/gaobiao/SeagateBackupPlusDrive/201/201-2018/data/guilin/hongling_Round1/hongling_round1_0.dsv";
string AVI_FILE = "/media/gaobiao/SeagateBackupPlusDrive/201/201-2018/data/guilin/hongling_Round1/0.avi";
string CAM_CALIB_FILE = "/home/gaobiao/Documents/RoadSegmentation_IV2019/src/DsvSegRegion/Sampledata-001-Camera.camera";

int START_TIME = 0;
int END_TIME = 1E10;

FILE *dfp;
FILE* fFileNameList;
std::vector<NAVDATA> nav;
bool camCalibFlag = true;
TRANSINFO calibInfo;
int dsbytesiz = sizeof (point3d)*2 + sizeof (ONEVDNDATA);
ONEDSVFRAME	*onefrm;
int dFrmNum;
int dFrmNo;
int waitkeydelay = 0;
ONEDSVDATA preFrameDsv;

vector<string> fileName;
cv::Mat videoImg;
cv::Mat inputImg;               // input image (BGR)
cv::Mat inputImgForVis;         // input image (BGR) for label visualization
cv::Mat originGtImg;            // ground truth image (BGR)
cv::Mat newGtImg;               // new ground truth (BGR), ready to be written
cv::Mat mergeImg;               // merge input & gt for visualization
cv::Mat annotatedImg;           // annotated layer (Gray)

double  SCALE_RATIO     = 2.0;
int     ACTIVE_LABEL    = 0;           // annotation label class
int     PEN_WIDTH       = 2;

void Init()
{
    // Read file name list of dataset
    fFileNameList = fopen(FILE_LIST_PATH.c_str(), "r");
    fileName.clear();
    char fName[20];
    while (fscanf(fFileNameList, "%s\n", fName) != EOF) {
        fileName.push_back(fName);
    }
    if (!LoadCalibFile (CALIB_FILE.c_str())) {
        printf ("Invalid calibration file\n");
        getchar ();
        exit (1);
    }
    // dsv
    if ((dfp = fopen(DSV_FILE.c_str(), "r")) == NULL) {
        printf("DSV file open failure\n");
        getchar ();
        exit (1);
    }
    // video
    cv::VideoCapture cap(AVI_FILE.c_str());
    FILE* tsFp = fopen((AVI_FILE+".ts").c_str(), "r");
    if (!cap.isOpened()) {
        printf("Error opening video stream or file.\n");
        getchar();
        exit(1);
    }
    // Camera/Velodyne calib file
    if(!LoadCameraCalib(CAM_CALIB_FILE.c_str())){
        printf("Open Camera Calibration files fails.\n");
        camCalibFlag = false;
    }
    LONGLONG fileSize = myGetFileSize(dfp);
    dFrmNum = fileSize / (BKNUM_PER_FRM) / dsbytesiz;
    onefrm = new ONEDSVFRAME[1];
    dFrmNo = 0;
    preFrameDsv.millisec = 0;

    cv::namedWindow("input");
    cv::namedWindow("video & point cloud");
    cv::namedWindow("newGT");
    cv::namedWindow("annotation");
    cv::namedWindow("human annotation");
    cv::namedWindow("video");
    cv::moveWindow("input", 20, 20);
    cv::moveWindow("annotation", 680, 20);
    cv::moveWindow("video & point cloud", 1340, 400);
    cv::moveWindow("video", 1340, 20);
    cv::moveWindow("newGT", 20, 680);
    cv::moveWindow("human annotation", 400, 680);
}

void ReadDsv(int ts)
{
    // Back up pre frame's dsv[0]
    if (preFrameDsv.millisec > 0) preFrameDsv = onefrm->dsv[0];
    if (onefrm->dsv[0].millisec == ts) return;

    ReadOneDsvFrame();
    dFrmNo ++;
    while (onefrm->dsv[0].millisec < ts) {
        // Read next frame
        ReadOneDsvFrame();
        dFrmNo ++;
    }
    while (onefrm->dsv[0].millisec > ts) {
        // Read previous frame
        dFrmNo = max(dFrmNo - 2, 0);
        fseeko64(dfp, (LONGLONG)dFrmNo * dsbytesiz * BKNUM_PER_FRM, SEEK_SET);
        ReadOneDsvFrame();
        dFrmNo ++;
    }

    // if it's the first frame, set preFrameDsv to the current frame data
    if (preFrameDsv.millisec == 0) preFrameDsv = onefrm->dsv[0];
}

void LabelImage(int x, int y)
{
    x = int(x / SCALE_RATIO);
    y = int(y / SCALE_RATIO);
    int c = 0;
    cv::Scalar cs(0, 0, 0);
    if (ACTIVE_LABEL == 0) {c = 0; cs = cv::Scalar(0, 0, 0);} // Unknown
    if (ACTIVE_LABEL == 1) {c = 1; cs = cv::Scalar(0, 255, 0);} // Passable
    if (ACTIVE_LABEL == 2) {c = 2; cs = cv::Scalar(0, 0, 255);} // Unpassable

    // Can't label the pixel without laser point
    if (inputImg.at<cv::Vec3b>(x, y)[0] == 0 &&
        inputImg.at<cv::Vec3b>(x, y)[1] == 0 &&
        inputImg.at<cv::Vec3b>(x, y)[2] == 0) {
        return;
    }
    cv::circle(annotatedImg, cv::Point(y, x), PEN_WIDTH, cv::Scalar(c), -1);
    cv::circle(inputImgForVis, cv::Point(y, x), PEN_WIDTH, cs, -1);
}

void UpdateVis()
{
    // Update newGT
    for (int i = 0; i < annotatedImg.rows; i ++) {
        for (int j = 0; j < annotatedImg.cols; j ++) {
            int ac = annotatedImg.at<uchar>(i, j);
            if (ac == 255) continue;
            if (ac == 0) {      // Unknown
                newGtImg.at<cv::Vec3b>(i, j)[0] = 0;
                newGtImg.at<cv::Vec3b>(i, j)[1] = 0;
                newGtImg.at<cv::Vec3b>(i, j)[2] = 0;
            }
            if (ac == 1) {      // Passable
                newGtImg.at<cv::Vec3b>(i, j)[0] = 0;
                newGtImg.at<cv::Vec3b>(i, j)[1] = 255;
                newGtImg.at<cv::Vec3b>(i, j)[2] = 0;
            }
            if (ac == 2) {      // Unpassable
                newGtImg.at<cv::Vec3b>(i, j)[0] = 0;
                newGtImg.at<cv::Vec3b>(i, j)[1] = 0;
                newGtImg.at<cv::Vec3b>(i, j)[2] = 255;
            }
        }
    }
    // Show image
    cv::Mat tmpImg;
    cv::addWeighted(inputImg, 0.7, newGtImg, 0.6, 0, mergeImg);
    cv::resize(mergeImg, tmpImg, cv::Size(mergeImg.cols * SCALE_RATIO, mergeImg.rows * SCALE_RATIO), 0, 0, INTER_NEAREST);
    cv::imshow("annotation", tmpImg);
    cv::imshow("human annotation", annotatedImg);
    cv::resize(inputImgForVis, tmpImg, cv::Size(inputImg.cols * SCALE_RATIO, inputImg.rows * SCALE_RATIO), 0, 0, INTER_NEAREST);
    cv::imshow("input", tmpImg);
}

void CallbackAnnotation(int event, int x, int y, int flags, void *param)
{
    if (event == CV_EVENT_LBUTTONDOWN || (flags & CV_EVENT_FLAG_LBUTTON)) {
        LabelImage(y, x);
        UpdateVis();
    }
    if (event == CV_EVENT_LBUTTONUP) {
        // Show point cloud projected to the video
        cv::Mat tmpImg = videoImg.clone();
        pointCloudsProject(tmpImg, newGtImg);
        cv::imshow("video & point cloud", tmpImg);
        cv::imshow("newGT", newGtImg);
        inputImgForVis = inputImg.clone();
        cv::resize(inputImgForVis, tmpImg, cv::Size(inputImg.cols * SCALE_RATIO, inputImg.rows * SCALE_RATIO), 0, 0, INTER_NEAREST);
        cv::imshow("input", tmpImg);
    }
}

void CallbackInput(int event, int x, int y, int flags, void *param)
{
    if (event == CV_EVENT_LBUTTONDOWN || (flags & CV_EVENT_FLAG_LBUTTON)) {
        LabelImage(y, x);
        UpdateVis();
    }
    if (event == CV_EVENT_LBUTTONUP) {
        // Show point cloud projected to the video
        cv::Mat tmpImg = videoImg.clone();
        pointCloudsProject(tmpImg, newGtImg);
        cv::imshow("video & point cloud", tmpImg);
        cv::imshow("newGT", newGtImg);
        inputImgForVis = inputImg.clone();
        cv::resize(inputImgForVis, tmpImg, cv::Size(inputImg.cols * SCALE_RATIO, inputImg.rows * SCALE_RATIO), 0, 0, INTER_NEAREST);
        cv::imshow("input", tmpImg);
    }
}
// Save new ground truth
void SaveNewGT(int idx)
{
    cv::Mat writtenGtImg(originGtImg.rows, originGtImg.cols, CV_8UC1);
    writtenGtImg.setTo(0);
    BGR2Gt(newGtImg, writtenGtImg);
    if (cv::imwrite((ANNOTATION_PATH + fileName[idx] + "_gt.png").c_str(), writtenGtImg)) {
        printf("%s_gt.png saved!\n", fileName[idx].c_str());
    }
    else {
        printf("save error!\n");
    }
}

int main()
{
    Init();

    for (int idx = 0; idx < fileName.size(); idx ++) {
        if (atoi(fileName[idx].c_str()) < START_TIME) continue;
        if (atoi(fileName[idx].c_str()) > END_TIME) break;
        if (atoi(fileName[idx].c_str()) == START_TIME) {
            dFrmNo = idx + 318;
            int err = fseeko64(dfp, (LONGLONG)dFrmNo * dsbytesiz * BKNUM_PER_FRM, SEEK_SET);
        }

        // Read DSV data
        ReadDsv(atoi(fileName[idx].c_str()));
        printf("no.%d, time: %s\n", dFrmNo, fileName[idx].c_str());

        // Read image data
        inputImg = cv::imread(UNANNOTATED_PATH + fileName[idx] + "_img.png", cv::IMREAD_COLOR);
        originGtImg = cv::imread(UNANNOTATED_PATH + fileName[idx] + "_gt.png", cv::IMREAD_COLOR);
        videoImg = cv::imread(UNANNOTATED_PATH + fileName[idx] + "_video.png", cv::IMREAD_COLOR);
        inputImgForVis = inputImg.clone();

        // Read human annotated ground truth
        cv::Mat readImg;
        readImg = cv::imread(ANNOTATION_PATH + fileName[idx] + "_gt.png");
        if (readImg.empty()) {
            newGtImg = originGtImg.clone();
            Gt2BGR(newGtImg);
            if (preFrameDsv.millisec != 0) {
                // transform the previous human annotation into the coordination of the newest position
                TransAnnotation(annotatedImg);
            }
        }
        else {
            newGtImg = readImg.clone();
            Gt2BGR(newGtImg);
        }
        if (annotatedImg.empty()) {
            annotatedImg = cv::Mat(inputImg.rows, inputImg.cols, CV_8UC1);
            annotatedImg.setTo(255);
        }

        // Show image
        cv::Mat tmpImg;
        UpdateVis();
        cv::resize(inputImgForVis, tmpImg, cv::Size(inputImg.cols * SCALE_RATIO, inputImg.rows * SCALE_RATIO), 0, 0, INTER_NEAREST);
        cv::imshow("input", tmpImg);
        cv::imshow("video", videoImg);

        // Show point cloud projected to the video
        tmpImg = videoImg.clone();
        pointCloudsProject(tmpImg, newGtImg);
        cv::imshow("newGT", newGtImg);
        cv::imshow("video & point cloud", tmpImg);

        cv::setMouseCallback("annotation", CallbackAnnotation);
        cv::setMouseCallback("input", CallbackInput);

        // Catch keyboard event
        int WaitKey = cv::waitKey(waitkeydelay);
        if (WaitKey == 27)        // ESC
            break;
        else
        if (WaitKey == 'z') {     // ��������
            if (waitkeydelay==1) waitkeydelay = 0;
                            else waitkeydelay = 1;
        }
        else
        if (WaitKey == 'a') {     // Back
            idx = max(idx - 2, -1);
            continue;
        }
        else
        if (WaitKey == 'd') {     // Forword
            continue;
        }
        else
        if (WaitKey == 'r') {       // Reset human annotation
            annotatedImg.setTo(255);
            std::remove((ANNOTATION_PATH + fileName[idx] + "_gt.png").c_str());
            idx --;
        }
        else
        if (WaitKey == '0') {       // change active label class
            ACTIVE_LABEL = 0;
            printf("Active Label : %c\n", WaitKey);
            idx --;
        }
        else
        if (WaitKey == '1') {       // change active label class
            ACTIVE_LABEL = 1;
            printf("Active Label : %c\n", WaitKey);
            idx --;
        }
        else
        if (WaitKey == '2') {       // change active label class
            ACTIVE_LABEL = 2;
            printf("Active Label : %c\n", WaitKey);
            idx --;
        }
        else
        if (WaitKey == 32) {        // space bar: save the human annotation & new ground truth
            SaveNewGT(idx);
        }
        else
        if (WaitKey != -1) {
            idx --;
        }
    }
    fclose(fFileNameList);
    return 0;
}