/* Copyright (c) 2014, Julian Straub <jstraub@csail.mit.edu>
 * Licensed under the MIT license. See the license file LICENSE.
 */

#pragma once

#include <cudaPcl/openniSmoothDepthGpu.hpp>
#include <cudaPcl/normalExtractSimpleGpu.hpp>
#include <cudaPcl/cv_helpers.hpp>

namespace cudaPcl {

/*
 * OpenniSmoothNormalsGpu smoothes the depth frame using a guided filter and
 * computes surface normals from it also on the GPU.
 *
 * Needs the focal length of the depth camera f_d and the parameters for the
 * guided filter eps as well as the filter size B.
 */
class OpenniSmoothNormalsGpu : public OpenniVisualizer
{
  public:
  OpenniSmoothNormalsGpu(double f_d, double eps, uint32_t B, bool
      compress=false)
    : OpenniVisualizer(true), eps_(eps), B_(B), f_d_(f_d),
    depthFilter(NULL), normalExtract(NULL), compress_(compress)
  { };

  virtual ~OpenniSmoothNormalsGpu() {
    if(normalExtract) delete normalExtract;
  };

  virtual void depth_cb(const uint16_t * depth, uint32_t w, uint32_t h)
  {
    if(w==0 || h==0) return;
    if(!depthFilter)
    {
      depthFilter = new DepthGuidedFilterGpu<float>(w,h,eps_,B_);
      normalExtract = new NormalExtractSimpleGpu<float>(f_d_,w,h,compress_);
    }
    cv::Mat dMap = cv::Mat(h,w,CV_16U,const_cast<uint16_t*>(depth));

//    Timer t;
    depthFilter->filter(dMap);
//    t.toctic("smoothing");
    normalExtract->computeGpu(depthFilter->getDepthDevicePtr());
//    t.toctic("normals");
    normals_cb(normalExtract->d_normalsImg(), normalExtract->d_haveData(),w,h);
//    t.toctic("normals callback");
    if(compress_)
    {
      int32_t nComp =0;
      normalsComp_ = normalExtract->normalsComp(nComp);
      std::cout << "# compressed normals " << nComp << std::endl;
    }
  };

  /* callback with smoothed normals
   *
   * Note that the pointers are to GPU memory as indicated by the "d_" prefix.
   */
  virtual void normals_cb(float* d_normalsImg, uint8_t* d_haveData,
      uint32_t w, uint32_t h)
  {
    if(w==0 || h==0) return;
    boost::mutex::scoped_lock updateLock(updateModelMutex);
    normalsImg_ = normalExtract->normalsImg();

    if(false)
    {
      static int frameN = 0;
      if(frameN==0) if(system("mkdir ./normals/") >0){
        cout<<"problem creating subfolder for results"<<endl;
      };

      char path[100];
      // Save the image data in binary format
      sprintf(path,"./normals/%05d.bin",frameN ++);
      if(compress_)
      {
        int nComp;
        normalsComp_ = normalExtract->normalsComp(nComp);
        imwriteBinary(std::string(path), normalsComp_);
      }else
        imwriteBinary(std::string(path), normalsImg_);
    }
    this->update_ = true;
  };

  virtual void visualizeD();
  virtual void visualizePC();

  protected:
  double eps_;
  uint32_t B_;
  double f_d_;
  DepthGuidedFilterGpu<float> * depthFilter;
  NormalExtractSimpleGpu<float> * normalExtract;
  bool compress_;
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr nDisp_;
  cv::Mat normalsImg_;
  cv::Mat nIRGB_;
  cv::Mat normalsComp_;
};
// ------------------------ impl -----------------------------------------
void OpenniSmoothNormalsGpu::visualizeD()
{
  if (this->depthFilter)
  {
    cv::Mat dSmooth = this->depthFilter->getOutput();
    this->dColor_ = colorizeDepth(dSmooth,0.3,4.0);
    cv::imshow("d",dColor_);
//    cv::Mat dNans = dSmooth.clone();
//    showNans(dNans);
//    cv::imshow("depth Nans",dNans);
  }
};

void OpenniSmoothNormalsGpu::visualizePC()
{
  if (normalsImg_.empty() || normalsImg_.rows == 0 || normalsImg_.cols
      == 0) return;
  cv::Mat nI (normalsImg_.rows,normalsImg_.cols, CV_8UC3);
//  cv::Mat nIRGB(normalsImg_.rows,normalsImg_.cols,CV_8UC3);
  normalsImg_.convertTo(nI, CV_8UC3, 127.5,127.5);
  cv::cvtColor(nI,nIRGB_,CV_RGB2BGR);
  cv::imshow("normals",nIRGB_);
  if (compress_)  cv::imshow("dcomp",normalsComp_);

  if (false) {
    // show additional diagnostics
    std::vector<cv::Mat> nChans(3);
    cv::split(normalsImg_,nChans);
    cv::Mat nNans = nChans[0].clone();
    showNans(nNans);
    cv::imshow("normal Nans",nNans);
    cv::Mat haveData = normalExtract->haveData();
    cv::imshow("haveData",haveData*200);
  }

#ifdef USE_PCL_VIEWER
  pc_ = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new
      pcl::PointCloud<pcl::PointXYZRGB>());
  for (uint32_t i=0; i<normalsImg_.rows; ++i) 
    for (uint32_t j=0; j<normalsImg_.cols; ++j) {
      pcl::PointXYZRGB p;
      p.x = normalsImg_.at<cv::Vec3f>(i,j)[0];
      p.y = normalsImg_.at<cv::Vec3f>(i,j)[1];
      p.z = normalsImg_.at<cv::Vec3f>(i,j)[2];
      p.rgb = 0;
      float norm = p.x*p.x+p.y*p.y+p.z*p.z;
      if (0.98 <= norm && norm <= 1.02) this->pc_->push_back(p);
    }
  if (this->pc_->size() > 0) {
    if(!this->viewer_->updatePointCloud(this->pc_, "pc"))
      this->viewer_->addPointCloud(this->pc_, "pc");
  }
#endif
}

} // namespace cudaPcl
