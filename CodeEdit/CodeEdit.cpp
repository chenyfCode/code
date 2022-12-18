//�궨��У����SIFTƥ�䡢���
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/highgui/highgui.hpp"
#include <iostream>
#include <fstream>
#include <opencv2\imgproc\types_c.h>

using namespace cv;
using namespace std;

//***************************�����Լ���ʼ��******************************//

const char* imageName_L = "../Images/1206-8mm-2602mm/A0.png"; // ���ڼ����ȵ�ͼ��
const char* imageName_R = "../Images/1206-8mm-2602mm/B0.png";
const char* stereoRectifyParams = "stereoRectifyParams.txt"; // �������У������
vector<vector<Point2f>> corners_seq_L; // ���нǵ�����
vector<vector<Point2f>> corners_seq_R;
vector<vector<Point3f>> objectPoints_L; // ��ά����
vector<vector<Point3f>> objectPoints_R;
Mat cameraMatrix_L = (cv::Mat_<float>(3, 3) << 1781.810262887706, 0, 604.4243823452152,
	0, 1780.163208758555, 493.8393294388836,
	0, 0, 1); // ��������ڲ���
Mat cameraMatrix_R = (cv::Mat_<float>(3, 3) << 1763.816741031971, 0, 617.9243882089885,
	0, 1763.387847013527, 474.5017098333396,
	0, 0, 1); // ��������ڲ���
//�����������ϵ��
Mat distCoeffs_L = (Mat_<float>(1, 5) << -0.08572684461447769, 0.2389193416004575, -0.004663266549834569, 0.0009870654966580672, -0.4788910294243767); // ������Ļ���ϵ��
Mat distCoeffs_R = (Mat_<float>(1, 5) << -0.07700464555060682, 0.2205726466952272, -0.004163407776421812, 0.0004392279361322822, -1.019433830263017);// ������Ļ���ϵ��
//��ת����
Mat R = (cv::Mat_<double>(3, 3) << 0.9990386966449903, -0.02052550993291603, 0.03873481699805289,
	0.01953812902031574, 0.9994787634885305, 0.02569947178102284,
	-0.03924212176048051, -0.02491796094049147, 0.9989189912612052);
//ƽ�ƾ���
Mat T = (cv::Mat_<double>(3, 1) << -188.4484020648055,
	3.018669006556458,
	-12.35880046938432);
//��������
Mat E = (cv::Mat_<float>(3, 3) << 0.1230088613972923, 12.27713953494406, 3.333020442891075,
	-19.74203505242429, -4.442079240155581, 187.7659718208645,
	-6.697696343127296, -188.288216156443, -4.959951982589974);
//��������
Mat F = (cv::Mat_<float>(3, 3) << -1.273661393890549e-08, -1.27237876738228e-06, 2.113126071972925e-05,
	2.044631852033703e-06, 4.604803897836992e-07, -0.03611309382934646,
	0.0002608862176085383, 0.03498654290771176, 1);
// ͼ��ߴ�
Size imageSize(1280,1024);



Mat R1, R2, P1, P2, Q; // ����У������
Mat mapl1, mapl2, mapr1, mapr2; // ͼ����ͶӰӳ���
Mat img1_rectified, img2_rectified, result3DImage; // У��ͼ��  ���ͼ

Rect validRoi[2];





//���㺺������
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

//RANSAC�㷨ʵ��
void ransac(vector<DMatch> matches, vector<KeyPoint> queryKeyPoint, vector<KeyPoint> trainKeyPoint, vector<DMatch>& matches_ransac)
{
	//���屣��ƥ��������
	vector<Point2f> srcPoints(matches.size()), dstPoints(matches.size());
	//����ӹؼ�������ȡ����ƥ���Ե�����
	for (int i = 0; i < matches.size(); i++)
	{
		srcPoints[i] = queryKeyPoint[matches[i].queryIdx].pt;
		dstPoints[i] = trainKeyPoint[matches[i].trainIdx].pt;
	}

	//ƥ���Խ���RANSAC����
	vector<int> inliersMask(srcPoints.size());
	//Mat homography;
	//homography = findHomography(srcPoints, dstPoints, RANSAC, 5, inliersMask);
	findHomography(srcPoints, dstPoints, RANSAC, 5, inliersMask);
	//�ֶ��ı���RANSAC���˺��ƥ����
	for (int i = 0; i < inliersMask.size(); i++)
		if (inliersMask[i])
			matches_ransac.push_back(matches[i]);
}



/*
����У��
������
	stereoRectifyParams	�������У�������txt
	cameraMatrix			����ڲ���
	distCoeffs				�������ϵ��
	imageSize				ͼ��ߴ�
	R						���������Ե���ת����
	T						���������Ե�ƽ������
	R1, R2					�ж�����תУ��
	P1, P2					����ͶӰ����
	Q						��ͶӰ����
	map1, map2				��ͶӰӳ���
*/

Rect stereoRectification(const char* stereoRectifyParams, Mat& cameraMatrix1, Mat& distCoeffs1, Mat& cameraMatrix2, Mat& distCoeffs2,
	Size& imageSize, Mat& R, Mat& T, Mat& R1, Mat& R2, Mat& P1, Mat& P2, Mat& Q, Mat& mapl1, Mat& mapl2, Mat& mapr1, Mat& mapr2)
{
	Rect validRoi[2];
	ofstream stereoStore(stereoRectifyParams);
	stereoRectify(cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, imageSize,
		R, T, R1, R2, P1, P2, Q, CALIB_ZERO_DISPARITY, 0, imageSize, &validRoi[0], &validRoi[1]);
	// ��������ͼ�����ͶӰӳ���
	stereoStore << "R1��" << endl;
	stereoStore << R1 << endl;
	stereoStore << "R2��" << endl;
	stereoStore << R2 << endl;
	stereoStore << "P1��" << endl;
	stereoStore << P1 << endl;
	stereoStore << "P2��" << endl;
	stereoStore << P2 << endl;
	stereoStore << "Q��" << endl;
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
�����Ӳ�ͼ
������
	imageName1	����������ͼ��
	imageName2	����������ͼ��
	img1_rectified	��ӳ����������ͼ��
	img2_rectified	��ӳ�����Ҳ����ͼ��
	map	��ͶӰӳ���
*/
bool computeDisparityImage(const char* imageName1, const char* imageName2, Mat& img1_rectified,
	Mat& img2_rectified, Mat& mapl1, Mat& mapl2, Mat& mapr1, Mat& mapr2, Rect validRoi[2])
{
	// ���ȣ����������������ͼƬ�����ع�
	Mat img1 = imread(imageName1);
	Mat img2 = imread(imageName2);
	if (img1.empty() | img2.empty())
	{
		cout << "ͼ��Ϊ��" << endl;
	}
	Mat gray_img1, gray_img2;
	cvtColor(img1, gray_img1, COLOR_BGR2GRAY);
	cvtColor(img2, gray_img2, COLOR_BGR2GRAY);
	Mat canvas(imageSize.height, imageSize.width * 2, CV_8UC1); // ע����������
	Mat canLeft = canvas(Rect(0, 0, imageSize.width, imageSize.height));
	Mat canRight = canvas(Rect(imageSize.width, 0, imageSize.width, imageSize.height));
	gray_img1.copyTo(canLeft);
	gray_img2.copyTo(canRight);
	imwrite("У��ǰ�������ͼ��.jpg", canvas);
	remap(gray_img1, img1_rectified, mapl1, mapl2, INTER_LINEAR);
	remap(gray_img2, img2_rectified, mapr1, mapr2, INTER_LINEAR);
	imwrite("�����У��ͼ��.jpg", img1_rectified);
	imwrite("�����У��ͼ��.jpg", img2_rectified);
	img1_rectified.copyTo(canLeft);
	img2_rectified.copyTo(canRight);
	rectangle(canLeft, validRoi[0], Scalar(255, 255, 255), 5, 8);
	rectangle(canRight, validRoi[1], Scalar(255, 255, 255), 5, 8);
	for (int j = 0; j <= canvas.rows; j += 16)
		line(canvas, Point(0, j), Point(canvas.cols, j), Scalar(0, 255, 0), 1, 8);
	imwrite("У�����������ͼ��.jpg", canvas);


	// ����SIFT��������ȡ��ƥ��
	int64 t1, t2;
	double tkpt, tdes, tmatch_bf, tmatch_knn;
	std::vector<cv::KeyPoint> keypoints1;
	std::vector<cv::KeyPoint> keypoints2;
	cv::Ptr<cv::SiftFeatureDetector> sift = cv::SiftFeatureDetector::create();

	// 2. ����������
	t1 = cv::getTickCount();
	sift->detect(img1_rectified, keypoints1);
	t2 = cv::getTickCount();
	tkpt = 1000.0 * (t2 - t1) / cv::getTickFrequency();
	sift->detect(img2_rectified, keypoints2);

	// 3. ��������������
	cv::Mat descriptors1, descriptors2;
	t1 = cv::getTickCount();
	sift->compute(img1_rectified, keypoints1, descriptors1);
	t2 = cv::getTickCount();
	tdes = 1000.0 * (t2 - t1) / cv::getTickFrequency();
	sift->compute(img2_rectified, keypoints2, descriptors2);

	// 4. ����ƥ��
	cv::Ptr<cv::DescriptorMatcher> matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE);
	//  ֱ�ӱ���ƥ��
	std::vector<cv::DMatch> matches;
	t1 = cv::getTickCount();
	matcher->match(descriptors1, descriptors2, matches);
	t2 = cv::getTickCount();
	tmatch_bf = 1000.0 * (t2 - t1) / cv::getTickFrequency();
	// ��ƥ��ͼ
	cv::Mat img_matches_bf;
	drawMatches(img1_rectified, keypoints1, img2_rectified, keypoints2, matches, img_matches_bf);
	imshow("����ƥ��", img_matches_bf);

	vector<DMatch>  good_min, good_ransac;
	//��С��������ɸѡ����ƥ����
	match_min(matches, good_min);
	cout << "good_min=" << good_min.size() << endl;
	//��ransac�㷨ɸѡ����ƥ����
	ransac(good_min, keypoints1, keypoints2, good_ransac);
	cout << "good_matches.size=" << good_ransac.size() << endl;
	Mat  outimg;
	drawMatches(img1_rectified, keypoints1, img2_rectified, keypoints2, good_ransac, outimg);
	imshow("ransac�㷨ɸѡ����ƥ����", outimg);

	/********���������β�෨ �������*******/
	double B = sqrt(T.at<double>(0, 0) * T.at<double>(0, 0) + T.at<double>(1, 0) * T.at<double>(1, 0) + T.at<double>(2, 0) * T.at<double>(2, 0));
	double f = Q.at<double>(2, 3);
	double avr_dis = 0;
	double max_dis = LONG_MIN, min_dis = LONG_MAX;
	cout << "f = " << f << endl;
	cout << "B = " << B << endl;
	cout << "********************************************" << endl;
	for (int i = 0; i < good_min.size(); i++)
	{
		double x_L = keypoints1[good_min[i].queryIdx].pt.x;
		double x_R = keypoints2[good_min[i].trainIdx].pt.x;

		double distance = f * B / abs(x_L - x_R);
		//cout << "x_L = " << x_L << endl;
		//cout << "x_R = " << x_R << endl;
		cout << "����Ϊ: " << distance << " mm" << endl;
	}


	return true;
}

// ���ص�����������Ӳ�ͼ��ʾ���
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
	//��������У��
	validRoi[0], validRoi[1] = stereoRectification(stereoRectifyParams, cameraMatrix_L, distCoeffs_L, cameraMatrix_R, distCoeffs_R,
		imageSize, R, T, R1, R2, P1, P2, Q, mapl1, mapl2, mapr1, mapr2);
	cout << "�Ѵ���ͼ����ͶӰӳ���" << endl;
	//SIFT��������ȡ��ƥ��
	computeDisparityImage(imageName_L, imageName_R, img1_rectified, img2_rectified, mapl1, mapl2, mapr1, mapr2, validRoi);

	waitKey(0);  //�ȴ���������
	return 0;
}



