

/* Image based visual servoing using three features
 * System : UR5 Robot with realsense camera mounted on end-effector
 * Author : Deepak Raina (Emp ID: 1445893)
 */

#include <ros/ros.h>
#include <ros/package.h>
#include <iostream>
#include <trajectory_msgs/JointTrajectory.h>
#include <trajectory_msgs/JointTrajectoryPoint.h>
#include <pcl_conversions/pcl_conversions.h>
//#include <control_speed/velocity_control.h>
//#include <moveit/move_group_interface/move_group.h>
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <my_funcs.h>

using namespace Eigen;
using namespace std;
using namespace cv;

ros::Publisher pb;
ros::Subscriber sb;
Mat image;

/*-------Main Function---------*/
int main (int argc, char** argv)
{
    ros::init(argc,argv,"ibvs_two_features");
    ros::NodeHandle nh;
    cv::startWindowThread();

    //joint states subscriber
    ros::Subscriber jointStateSub = nh.subscribe("/joint_states",10,jointStateCallback);
    //publisher for velocity
    ros::Publisher jointVelPub = nh.advertise<trajectory_msgs::JointTrajectory>("/ur_driver/joint_speed", 10);
    trajectory_msgs::JointTrajectory traj;
    trajectory_msgs::JointTrajectoryPoint trajPoint;
    traj.header.seq = 0;
    //publisher for velocity - seperate thread
    //ros::Publisher velPub= nh.advertise<control_speed::velocity_control>("/calc_velocity",10);

    //image features subscriber
    ros::Subscriber imgFeaturesSub = nh.subscribe<object_detection::image_data>("/object_points", 10, imgFeatureCallback);
    sleep(1);

    //UR-Driver Publisher (to move robot to initial position) - for a lazy person like me :-)
    ros::Publisher URDriver_pub = nh.advertise<std_msgs::String>("/ur_driver/URScript", 1);
    sleep(1);
    while(URDriver_pub.getNumSubscribers() == 0)
    {
        //cout << URDriver_pub.getNumSubscribers() << endl;
        cout << "No subscriber for UR-Driver" << endl;
    }
    cout << "Yes, I got subscriber for UR-Driver" << endl;
    std_msgs::String URScript;
    //URScript.data = "movej([0.02,-1.69,1.24,-1.13,-1.47,-0.03],a=0.34,v=0.17)"; //
    //URScript.data = "movej([0.30,-1.52,0.65,-0.70,-1.47,0.25],a=0.34,v=0.17)"; //
    //URScript.data = "movej([0.18,-1.77,1.31,-1.13,-1.47,0.14],a=0.34,v=0.17)"; //
    URScript.data = "movej([-1.57,-1.05,1.31,-3.49,-1.57,0.00],a=0.34,v=0.17)"; //bend 2
    URDriver_pub.publish(URScript);
    cout << "Press enter when robot will reach its initial position" << endl;
    getchar();


    //declaration of varibales
    Mat Error = Mat(3, 1, CV_64F, 0.0);
    Mat Pterm = Mat(3, 1, CV_64F, 0.0);
    Mat Iterm = Mat(3, 1, CV_64F, 0.0);
    Mat Dterm = Mat(3, 1, CV_64F, 0.0);
    Mat Pre_Error = Mat(3, 1, CV_64F, 0.0);
    Mat Error_PID = Mat(3, 1, CV_64F, 0.0);

    Mat Vc = Mat(6, 1, CV_64F, 0.0);
    Mat Ve = Mat(6, 1, CV_64F, 0.0);
    Mat Vb = Mat(6, 1, CV_64F, 0.0);

    Mat Jg = Mat(6, 6, CV_64F, 0.0);
    Mat Jg_inv = Mat(6, 6, CV_64F, 0.0);
    Mat theta_dot = Mat(6,1, CV_64F, 0.0);

    //PID gains
    double kpx=1.0, kdx=0.0, kix=0.0;
    double kpy=1.0, kdy=0.0, kiy=0.0;
    double kpz=0.0001, kdz=0.0, kiz=0.0;
    double dt=0,duration=0; //time and duration
    double lambda=531.15; //focal-length of camera

    //Getting current feature
    ros::spinOnce();
    double u_c[2]={0,0};
    u_c[0]=x_point, u_c[1]=y_point;
    double depth_p[2]={0,0}, prev_depth[2]; //depth of centroid
    //depth_p[0]=(double)centroid_depth, depth_p[1]=(double)centroid_depth;
    depth_p[0]=0.7, depth_p[1]=0.7;
    prev_depth[0]=depth_p[0], prev_depth[1]=depth_p[1];

    double m[3]={0,0,0};
    m[0]= (u_c[0]-320)/(531.15/640);    //(u-u0)/px
    m[1]= (u_c[1]-240)/(531.15/480);    //(v-v0)/py
    m[2]= centroid_depth;

    //Desired image features
    double u_d[2]={0,0};
    u_d[0] = 320; u_d[1] = 240;
    double centroid_depth_desired = 0.5;
    double m_d[3]={0,0,0};
    m_d[0]= (u_d[0]-320)/(531.15/640);    //(u-u0)/px
    m_d[1]= (u_d[1]-240)/(531.15/480);    //(v-v0)/py
    m_d[2]= centroid_depth_desired;

    //Error
    double mean_error = 0;
    for(int i = 0; i < 3; i++)
    {
        Error.at<double>(i,0) = m[i]-m_d[i];
        mean_error = mean_error + Error.at<double>(i,0)*Error.at<double>(i,0);
    }
    double sqrt_mean_error = sqrt(mean_error);
    Error_PID.at<double>(0,0)=kpx*Error.at<double>(0,0);
    Error_PID.at<double>(1,0)=kpy*Error.at<double>(1,0);
    Error_PID.at<double>(2,0)=kpz*Error.at<double>(2,0);

    cout << "-------------------------t="<<duration<<"---------------------------" << endl;
    cout << "Current Feature = [" << m[0] << " " << m[1] << " " << m[2] << "]" << endl;
    cout << "Desired Feature = [" << m_d[0] << " " << m_d[1] << " " << m_d[2]  << "]" << endl;
    cout << "Mean Error = " << sqrt_mean_error << endl;
    cout << "-------------------------------------------------------" << endl;
    cout << "Press enter to start visual servoing " << endl;
    getchar();

    //file for writing image features
    ofstream f1, f2, f3;
    f1.open("/home/mithun/ur5_visual_servoing/src/ur5_vs/src/results/trackedfeatures.txt",std::ios_base::trunc);
    f2.open("/home/mithun/ur5_visual_servoing/src/ur5_vs/src/results/joint_velocities.txt",std::ios_base::trunc);
    f3.open("/home/mithun/ur5_visual_servoing/src/ur5_vs/src/results/error.txt",std::ios_base::trunc);

    f1 << duration << "\t" << m[0] << "\t" << m[1] << "\t" << m[2] << endl;
    f2 << duration << "\t" << theta_dot.at<double>(0) << "\t"<< theta_dot.at<double>(1) << "\t"<< theta_dot.at<double>(2) << "\t"<< theta_dot.at<double>(3) << "\t"<< theta_dot.at<double>(4) << "\t"<< theta_dot.at<double>(5) << endl;  //saving joint velocities velocities to file
    f3 << duration << "\t" << abs(Error.at<double>(0,0)) << "\t"<< abs(Error.at<double>(1,0)) << "\t" << abs(Error.at<double>(2,0)) << "\t" << sqrt_mean_error << endl;  //saving error to file

    double total_time = ros::Time::now().toSec();
    ros::Rate rate(100);

    while(ros::ok())
    {
        double time = ros::Time::now().toSec();
        //feature velocity to camera velocity
        Vc.at<double>(0,0)= (depth_p[0]/lambda)*Error_PID.at<double>(0,0);
        Vc.at<double>(1,0)= (depth_p[1]/lambda)*Error_PID.at<double>(1,0);
        Vc.at<double>(2,0)= -Error_PID.at<double>(2,0);
        //cout << "Velocity: camera_frame  " << Vc << endl;
        //duration = ros::Time::now().toSec() - total_time;
        //cout << "-------------------------t1="<<duration<<"---------------------" << endl;


        //Convert velocity from camera frame to end-effector frame
        newconvertVelCameratoEEframe(Vc,Ve);
        //duration = ros::Time::now().toSec() - total_time;
        //cout << "-------------------------t2="<<duration<<"---------------------" << endl;
        //cout << "Velocity: eelink_frame  " << Ve << endl;

        //Convert velocity from end-effector frame to base frame
        newConvertVelEEtoBaseframe(Vc,Vb);
        //duration = ros::Time::now().toSec() - total_time;
        //cout << "-------------------------t3="<<duration<<"---------------------" << endl;
        //cout << "Velocity: base_frame " << Vb << endl;

        //Obtaining robot Jacobian
        double th[6]={jp1, jp2, jp3, jp4, jp5, jp6};
        //cout << "current joint angles " << jp1 << " " << jp2 << " " << jp3 << " " << jp4 << " " << jp5 << " " << jp6 << endl;
        newgetRobotJacobian(th,Jg);
        //duration = ros::Time::now().toSec() - total_time;
        //cout << "-------------------------t4="<<duration<<"---------------------" << endl;
        //ROS_INFO_STREAM("Geometric Jacobian " << endl << Jg);

        //Obtaining joint velocities
        cv::invert(Jg, Jg_inv, cv::DECOMP_SVD);
        theta_dot = Jg_inv * Vb;
        cout << "Joint velocity " << theta_dot << endl;
        //f2 << duration << "\t" << theta_dot.at<double>(0) << "\t"<< theta_dot.at<double>(1) << "\t"<< theta_dot.at<double>(2) << "\t"<< theta_dot.at<double>(3) << "\t"<< theta_dot.at<double>(4) << "\t"<< theta_dot.at<double>(5) << endl;  //saving joint velocities velocities to file
        //duration = ros::Time::now().toSec() - total_time;
        //cout << "-------------------------t5="<<duration<<"---------------------" << endl;

        //Sending velocity to controller
        traj.points.clear();
        trajPoint.velocities.clear();
        trajPoint.accelerations.clear();
        //applying velocity and acceleartion limit
        double vel_limit = 3.0; //i.e. 3 radian/sec
        double acc_limit[6] = {5.0,4.0,4.0,4.0,3.0,5.0}; //i.e. 4 radian/sec^2

        //AMAX=[5.0,4.0,4.0,4.0,3.0,5.0]
        //applying field of view limits
        double xmin=-320, xmax=320, ymin=-240, ymax=240;

        if (((m[0]>=xmin && m[0]<=xmax) && (m[1]>=ymin && m[1]<=ymax)) && (abs(theta_dot.at<double>(0,0))<vel_limit && abs(theta_dot.at<double>(1,0))<vel_limit && abs(theta_dot.at<double>(2,0))<vel_limit && abs(theta_dot.at<double>(3,0))<vel_limit && abs(theta_dot.at<double>(4,0))<vel_limit && abs(theta_dot.at<double>(5,0))<vel_limit))
        {
            trajPoint.velocities.push_back(theta_dot.at<double>(0));
            trajPoint.velocities.push_back(theta_dot.at<double>(1));
            trajPoint.velocities.push_back(theta_dot.at<double>(2));
            trajPoint.velocities.push_back(theta_dot.at<double>(3));
            trajPoint.velocities.push_back(theta_dot.at<double>(4));
            trajPoint.velocities.push_back(theta_dot.at<double>(5));

            trajPoint.accelerations.push_back(acc_limit[0]);
            trajPoint.accelerations.push_back(acc_limit[1]);
            trajPoint.accelerations.push_back(acc_limit[2]);
            trajPoint.accelerations.push_back(acc_limit[3]);
            trajPoint.accelerations.push_back(acc_limit[4]);
            trajPoint.accelerations.push_back(acc_limit[5]);
        }
        else
        {
            ROS_ERROR("Joint Velocity limit exceeded !");
            trajPoint.velocities.push_back(0);
            trajPoint.velocities.push_back(0);
            trajPoint.velocities.push_back(0);
            trajPoint.velocities.push_back(0);
            trajPoint.velocities.push_back(0);
            trajPoint.velocities.push_back(0);

            trajPoint.accelerations.push_back(0);
            trajPoint.accelerations.push_back(0);
            trajPoint.accelerations.push_back(0);
            trajPoint.accelerations.push_back(0);
            trajPoint.accelerations.push_back(0);
            trajPoint.accelerations.push_back(0);
        }


        //cout << "Press enter to start robot ... " << endl;
        //getchar();
        trajPoint.time_from_start = ros::Duration(1.0);
        traj.header.seq = traj.header.seq+1;
        traj.header.stamp = ros::Time::now();
        traj.points.push_back(trajPoint);
        jointVelPub.publish(traj); //joint speed publisher
        rate.sleep();

        //Sending velocity to controller
        //        control_speed::velocity_control velocity;
        //        if (abs(theta_dot.at<double>(0,0))<vel_limit && abs(theta_dot.at<double>(1,0))<vel_limit && abs(theta_dot.at<double>(2,0))<vel_limit && abs(theta_dot.at<double>(3,0))<vel_limit && abs(theta_dot.at<double>(4,0))<vel_limit && abs(theta_dot.at<double>(5,0))<vel_limit )

        //        {
        //            velocity.vel0.data = theta_dot.at<double>(0);
        //            velocity.vel1.data = theta_dot.at<double>(1);
        //            velocity.vel2.data = theta_dot.at<double>(2);
        //            velocity.vel3.data = theta_dot.at<double>(3);
        //            velocity.vel4.data = theta_dot.at<double>(4);
        //            velocity.vel5.data = theta_dot.at<double>(5);
        //        }
        //        else
        //        {
        //            velocity.vel0.data = 0;
        //            velocity.vel1.data = 0;
        //            velocity.vel2.data = 0;
        //            velocity.vel3.data = 0;
        //            velocity.vel4.data = 0;
        //            velocity.vel5.data = 0;
        //        }

        //        //Publishing velocity
        //        velPub.publish(velocity);

        f2 << duration << "\t" << jv1 << "\t"<< jv2 << "\t"<< jv3 << "\t"<< jv4 << "\t"<< jv5 << "\t"<< jv6 << endl;  //saving joint velocities velocities to file

        //finding current error
        ros::spinOnce();
        u_c[0]=x_point;
        u_c[1]=y_point;

        cout << "xy " << x_point << " " << y_point << endl;
        cout << "centroid_depth " << centroid_depth << endl;

        m[0]= (u_c[0]-320)/(531.15/640);    //(u-u0)/px
        m[1]= (u_c[1]-240)/(531.15/480);    //(v-v0)/py

        if (isnan(centroid_depth))
        {
            m[2]= centroid_depth_desired;
            depth_p[0]=centroid_depth_desired;
            depth_p[1]=centroid_depth_desired;
        }
        else
        {
            m[2]= (double)centroid_depth;
            depth_p[0]=(double)centroid_depth;
            depth_p[1]=(double)centroid_depth;
        }
        prev_depth[0] = depth_p[0],prev_depth[1] = depth_p[1];
        Error.copyTo(Pre_Error);

        mean_error=0;
        for(int i = 0; i < 3; i++)
        {
            Error.at<double>(i,0) = m[i]-m_d[i];
            mean_error = mean_error + Error.at<double>(i,0)*Error.at<double>(i,0);
        }
        sqrt_mean_error = sqrt(mean_error);
        f3 << duration << "\t" << abs(Error.at<double>(0,0)) << "\t"<< abs(Error.at<double>(1,0)) << "\t" << abs(Error.at<double>(2,0)) << "\t" << sqrt_mean_error << endl;  //saving error to file

        duration = ros::Time::now().toSec() - total_time;
        cout << "-------------------------t="<<duration<<"---------------------------" << endl;
        cout << "Current Feature = [" << m[0] << " " << m[1] << " " << m[2] << "]" << endl;
        cout << "Desired Feature = [" << m_d[0] << " " << m_d[1] << " " << m_d[2]  << "]" << endl;
        cout << "Mean Error = " << sqrt_mean_error << endl;
        cout << "-------------------------------------------------------" << endl;
        f1 << duration << "\t" << m[0] << "\t" << m[1] << "\t" << m[2] << "\n";

        if (sqrt_mean_error == 0)
            break;

        //Implement PID
        dt = ros::Time::now().toSec() - time;
        cout << "dt " << dt << endl;
        Pterm = Error; //Proportional term
        Iterm += Error*dt; //Integral term
        Dterm = (1/dt)*((Error-Pre_Error)); //Derivative term
        //Error_PID = kp*Pterm + ki*Iterm + kd*Dterm;
        Error_PID.at<double>(0,0)=kpx*Pterm.at<double>(0,0)+kix*Iterm.at<double>(0,0)+kdx*Dterm.at<double>(0,0);
        Error_PID.at<double>(1,0)=kpy*Pterm.at<double>(1,0)+kiy*Iterm.at<double>(1,0)+kdy*Dterm.at<double>(1,0);
        Error_PID.at<double>(2,0)=kpz*Pterm.at<double>(2,0)+kiz*Iterm.at<double>(2,0)+kdz*Dterm.at<double>(2,0);

        cout << "PID terms " << Pterm << " " << Iterm << " " << Dterm << endl;
        //cout << "Error PID terms " << kp*Pterm << " " << ki*Iterm << " " << kd*Dterm << endl;
        cout << "Error PID " << Error_PID << endl;

        //cout << "Check values and then press enter ... " << endl;
        //getchar();
    }
    f1.close();
    f2.close();
    f3.close();
    trajPoint.velocities.clear();
    trajPoint.accelerations.clear();
    traj.points.clear();
    cout << "Reached Target Feature in " << duration << " seconds" << endl;
    return 0;
}



