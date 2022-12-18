//标定、校正、SIFT匹配、测距
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/highgui/highgui.hpp"
#include <iostream>
#include <fstream>
#include <opencv2\imgproc\types_c.h>

using namespace cv;
using namespace std;

//***************************参数以及初始化******************************//

const char* imageName_L = "../Images/1206-8mm-2602mm/A0.png"; // 用于检测深度的图像
const char* imageName_R = "../Images/1206-8mm-2602mm/B0.png";
const char* stereoRectifyParams = "stereoRectifyParams.txt"; // 存放立体校正参数


Mat cameraMatrix_L = (cv::Mat_<float>(3, 3) << 1781.810262887706, 0, 604.4243823452152,
	                                           0, 1780.163208758555, 493.8393294388836,
	                                           0, 0, 1); // 左相机的内参数
Mat cameraMatrix_R = (cv::Mat_<float>(3, 3) << 1763.816741031971, 0, 617.9243882089885,
	                                           0, 1763.387847013527, 474.5017098333396,
	                                           0, 0, 1); // 右相机的内参数
//左右相机畸变系数
Mat distCoeffs_L = (Mat_<float>(1, 5) << -0.08572684461447769, 0.2389193416004575, -0.004663266549834569, 0.0009870654966580672, -0.4788910294243767); // 左相机的畸变系数
Mat distCoeffs_R = (Mat_<float>(1, 5) << -0.07700464555060682, 0.2205726466952272, -0.004163407776421812, 0.0004392279361322822, -1.019433830263017);// 右相机的畸变系数
//旋转矩阵
Mat R = (cv::Mat_<double>(3, 3) << 0.9990386966449903, -0.02052550993291603, 0.03873481699805289,
	0.01953812902031574, 0.9994787634885305, 0.02569947178102284,
	-0.03924212176048051, -0.02491796094049147, 0.9989189912612052);
//平移矩阵
Mat T = (cv::Mat_<double>(3, 1) << -188.4484020648055,
	3.018669006556458,
	-12.35880046938432);
//本征矩阵
Mat E = (cv::Mat_<float>(3, 3) << 0.1230088613972923, 12.27713953494406, 3.333020442891075,
	-19.74203505242429, -4.442079240155581, 187.7659718208645,
	-6.697696343127296, -188.288216156443, -4.959951982589974);
//基础矩阵
Mat F = (cv::Mat_<float>(3, 3) << -1.273661393890549e-08, -1.27237876738228e-06, 2.113126071972925e-05,
	2.044631852033703e-06, 4.604803897836992e-07, -0.03611309382934646,
	0.0002608862176085383, 0.03498654290771176, 1);
// 图像尺寸
Size imageSize(1280,1024);



Mat R1, R2, P1, P2, Q; // 立体校正参数
Mat mapl1, mapl2, mapr1, mapr2; // 图像重投影映射表
Mat img1_rectified, img2_rectified, result3DImage; // 校正图像  深度图

Rect validRoi[2];





//计算汉明距离
void match_min(vector<DMatch> matches, vector<DMatch>& good_matches)
{
	double min_dist = 10000, max_dist = 0;
	for (int i = 0; i < matches.size(); i++)
	{
		double dist = matches[i].distance;
		if (dist < min_dist) min_dist = dist;
		if (dist > max_dist) max_dist = dist;
	}
	cout << "min_dist=" << min_dist << endl;
	cout << "max_dist=" << max_dist << endl;

	for (int i = 0; i < matches.size(); i++)
		if (matches[i].distance <= max(2 * min_dist, 20.0))
			good_matches.push_back(matches[i]);
}

//RANSAC算法实现
void ransac(vector<DMatch> matches, vector<KeyPoint> queryKeyPoint, vector<KeyPoint> trainKeyPoint, vector<DMatch>& matches_ransac)
{
	//定义保存匹配点对坐标
	vector<Point2f> srcPoints(matches.size()), dstPoints(matches.size());
	//保存从关键点中提取到的匹配点对的坐标
	for (int i = 0; i < matches.size(); i++)
	{
		srcPoints[i] = queryKeyPoint[matches[i].queryIdx].pt;
		dstPoints[i] = trainKeyPoint[matches[i].trainIdx].pt;
	}

	//匹配点对进行RANSAC过滤
	vector<int> inliersMask(srcPoints.size());
	//Mat homography;
	//homography = findHomography(srcPoints, dstPoints, RANSAC, 5, inliersMask);
	findHomography(srcPoints, dstPoints, RANSAC, 5, inliersMask);
	//手动的保留RANSAC过滤后的匹配点对
	for (int i = 0; i < inliersMask.size(); i++)
		if (inliersMask[i])
			matches_ransac.push_back(matches[i]);
}



/*
立体校正
参数：
	stereoRectifyParams	存放立体校正结果的txt
	cameraMatrix			相机内参数
	distCoeffs				相机畸变系数
	imageSize				图像尺寸
	R						左右相机相对的旋转矩阵
	T						左右相机相对的平移向量
	R1, R2					行对齐旋转校正
	P1, P2					左右投影矩阵
	Q						重投影矩阵
	map1, map2				重投影映射表
*/

Rect stereoRectification(const char* stereoRectifyParams, Mat& cameraMatrix1, Mat& distCoeffs1, Mat& cameraMatrix2, Mat& distCoeffs2,
	Size& imageSize, Mat& R, Mat& T, Mat& R1, Mat& R2, Mat& P1, Mat& P2, Mat& Q, Mat& mapl1, Mat& mapl2, Mat& mapr1, Mat& mapr2)
{
	Rect validRoi[2];
	ofstream stereoStore(stereoRectifyParams);
	stereoRectify(cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, imageSize,
		R, T, R1, R2, P1, P2, Q, CALIB_ZERO_DISPARITY, 0, imageSize, &validRoi[0], &validRoi[1]);
	// 计算左右图像的重投影映射表
	stereoStore << "R1：" << endl;
	stereoStore << R1 << endl;
	stereoStore << "R2：" << endl;
	stereoStore << R2 << endl;
	stereoStore << "P1：" << endl;
	stereoStore << P1 << endl;
	stereoStore << "P2：" << endl;
	stereoStore << P2 << endl;
	stereoStore << "Q：" << endl;
	stereoStore << Q << endl;
	stereoStore.close();
	cout << "R1:" << endl;
	cout << R1 << endl;
	cout << "R2:" << endl;
	cout << R2 << endl;
	cout << "P1:" << endl;
	cout << P1 << endl;
	cout << "P2:" << endl;
	cout << P2 << endl;
	cout << "Q:" << endl;
	cout << Q << endl;
	initUndistortRectifyMap(cameraMatrix1, distCoeffs1, R1, P1, imageSize, CV_32FC1, mapl1, mapl2);
	initUndistortRectifyMap(cameraMatrix2, distCoeffs2, R2, P2, imageSize, CV_32FC1, mapr1, mapr2);
	return validRoi[0], validRoi[1];
}

/*
计算视差图
参数：
	imageName1	左相机拍摄的图像
	imageName2	右相机拍摄的图像
	img1_rectified	重映射后的左侧相机图像
	img2_rectified	重映射后的右侧相机图像
	map	重投影映射表
*/
bool computeDisparityImage(const char* imageName1, const char* imageName2, Mat& img1_rectified,
	Mat& img2_rectified, Mat& mapl1, Mat& mapl2, Mat& mapr1, Mat& mapr2, Rect validRoi[2])
{
	// 首先，对左右相机的两张图片进行重构
	Mat img1 = imread(imageName1);
	Mat img2 = imread(imageName2);
	if (img1.empty() | img2.empty())
	{
		cout << "图像为空" << endl;
	}
	Mat gray_img1, gray_img2;
	cvtColor(img1, gray_img1, COLOR_BGR2GRAY);
	cvtColor(img2, gray_img2, COLOR_BGR2GRAY);
	Mat canvas(imageSize.height, imageSize.width * 2, CV_8UC1); // 注意数据类型
	Mat canLeft = canvas(Rect(0, 0, imageSize.width, imageSize.height));
	Mat canRight = canvas(Rect(imageSize.width, 0, imageSize.width, imageSize.height));
	gray_img1.copyTo(canLeft);
	gray_img2.copyTo(canRight);
	imwrite("校正前左右相机图像.jpg", canvas);
	remap(gray_img1, img1_rectified, mapl1, mapl2, INTER_LINEAR);
	remap(gray_img2, img2_rectified, mapr1, mapr2, INTER_LINEAR);
	imwrite("左相机校正图像.jpg", img1_rectified);
	imwrite("右相机校正图像.jpg", img2_rectified);
	img1_rectified.copyTo(canLeft);
	img2_rectified.copyTo(canRight);
	rectangle(canLeft, validRoi[0], Scalar(255, 255, 255), 5, 8);
	rectangle(canRight, validRoi[1], Scalar(255, 255, 255), 5, 8);
	for (int j = 0; j <= canvas.rows; j += 16)
		line(canvas, Point(0, j), Point(canvas.cols, j), Scalar(0, 255, 0), 1, 8);
	imwrite("校正后左右相机图像.jpg", canvas);


	// 进行SIFT特征点提取与匹配
	int64 t1, t2;
	double tkpt, tdes, tmatch_bf, tmatch_knn;
	std::vector<cv::KeyPoint> keypoints1;
	std::vector<cv::KeyPoint> keypoints2;
	cv::Ptr<cv::SiftFeatureDetector> sift = cv::SiftFeatureDetector::create();

	// 2. 计算特征点
	t1 = cv::getTickCount();
	sift->detect(img1_rectified, keypoints1);
	t2 = cv::getTickCount();
	tkpt = 1000.0 * (t2 - t1) / cv::getTickFrequency();
	sift->detect(img2_rectified, keypoints2);

	// 3. 计算特征描述符
	cv::Mat descriptors1, descriptors2;
	t1 = cv::getTickCount();
	sift->compute(img1_rectified, keypoints1, descriptors1);
	t2 = cv::getTickCount();
	tdes = 1000.0 * (t2 - t1) / cv::getTickFrequency();
	sift->compute(img2_rectified, keypoints2, descriptors2);

	// 4. 特征匹配
	cv::Ptr<cv::DescriptorMatcher> matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE);
	//  直接暴力匹配
	std::vector<cv::DMatch> matches;
	t1 = cv::getTickCount();
	matcher->match(descriptors1, descriptors2, matches);
	t2 = cv::getTickCount();
	tmatch_bf = 1000.0 * (t2 - t1) / cv::getTickFrequency();
	// 画匹配图
	cv::Mat img_matches_bf;
	drawMatches(img1_rectified, keypoints1, img2_rectified, keypoints2, matches, img_matches_bf);
	imshow("暴力匹配", img_matches_bf);

	vector<DMatch>  good_min, good_ransac;
	//最小汉明距离筛选暴力匹配结果
	match_min(matches, good_min);
	cout << "good_min=" << good_min.size() << endl;
	//用ransac算法筛选暴力匹配结果
	ransac(good_min, keypoints1, keypoints2, good_ransac);
	cout << "good_matches.size=" << good_ransac.size() << endl;
	
	Mat  outimg;
	drawMatches(img1_rectified, keypoints1, img2_rectified, keypoints2, good_ransac, outimg, 
		        cv::Scalar::all(-1), cv::Scalar::all(-1), vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
	

	/********相似三角形测距法 计算距离*******/
	double B = sqrt(T.at<double>(0, 0) * T.at<double>(0, 0) + T.at<double>(1, 0) * T.at<double>(1, 0) + T.at<double>(2, 0) * T.at<double>(2, 0));
	double f = Q.at<double>(2, 3);
	double avr_dis = 0;
	double max_dis = LONG_MIN, min_dis = LONG_MAX;
	Point origin;
	cout << "f = " << f << endl;
	cout << "B = " << B << endl;
	cout << "********************************************" << endl;
	for (int i = 0; i < good_min.size(); i++)
	{
		double x_L = keypoints1[good_min[i].queryIdx].pt.x;
		double x_R = keypoints2[good_min[i].trainIdx].pt.x;
		origin.x = keypoints1[good_min[i].queryIdx].pt.x;
		origin.y = keypoints1[good_min[i].queryIdx].pt.y;

		putText(outimg, to_string(good_min[i].queryIdx), origin, cv::FONT_HERSHEY_SIMPLEX, 0.4, Scalar(0, 0, 255), 1);
		double distance = f * B / abs(x_L - x_R);
		
		cout << to_string(good_min[i].queryIdx)<< " 点的x坐标为: " << x_L<<" 求得的距离为： "<< distance << " mm" << endl;
	}
	imshow("ransac算法筛选暴力匹配结果", outimg);

	return true;
}

// 鼠标回调函数，点击视差图显示深度
void onMouse(int event, int x, int y, int flags, void* param)
{
	Point point;
	point.x = x;
	point.y = y;
	if (event == EVENT_LBUTTONDOWN)
	{
		cout << result3DImage.at<Vec3f>(point) << endl;
	}
}

int main()
{
	//进行立体校正
	validRoi[0], validRoi[1] = stereoRectification(stereoRectifyParams, cameraMatrix_L, distCoeffs_L, cameraMatrix_R, distCoeffs_R,
		imageSize, R, T, R1, R2, P1, P2, Q, mapl1, mapl2, mapr1, mapr2);
	cout << "已创建图像重投影映射表！" << endl;
	//SIFT特征点提取与匹配
	computeDisparityImage(imageName_L, imageName_R, img1_rectified, img2_rectified, mapl1, mapl2, mapr1, mapr2, validRoi);

	waitKey(0);  //等待键盘输入
	return 0;
}



