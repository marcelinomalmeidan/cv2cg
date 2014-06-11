/************************************************************************\

  Copyright 2011 The University of Michigan.
  All Rights Reserved.

  Permission to use, copy, modify and distribute this software
  and its documentation for educational, research and non-profit
  purposes, without fee, and without a written agreement is
  hereby granted, provided that the above copyright notice and
  the following paragraph appear in all copies.

  THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY OF MICHIGAN "AS IS" AND 
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY OF MICHIGAN
  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  
  Authors:

			Chen Feng
            Laboratory for Interactive Visualization in Engineering (LIVE)
			Department of Civil and Environmental Engineering
            2350 Hayward Street, Suite 2340 GG Brown Building
            University of Michigan
            Ann Arbor, MI 48109-2125
			Phone:    (734)764-8495
			EMail:    simbaforrest@gmail.com
			WWW site: http://www.umich.edu/~cforrest
            
			Vineet R. Kamat
            Laboratory for Interactive Visualization in Engineering (LIVE)
			Department of Civil and Environmental Engineering
            2350 Hayward Street, Suite 2340 GG Brown Building
            University of Michigan
            Ann Arbor, MI 48109-2125
            Phone:    (734)764-4325
			EMail:    vkamat@umich.edu
			WWW site: http://pathfinder.engin.umich.edu

\************************************************************************/

#include <iostream>
#include <limits>
#include <string>
#include <fstream>
#include <sstream>

#include "AllHelpers.h"

#define ESM_ORIGINAL //define this if you have original implementation of ESM
#define ESM_DEBUG 1
#define KEG_DEBUG 0
#define TAG_DEBUG_DRAW 0

#include "keg/KEGTracker.hpp"

#include "apriltag/apriltag.hpp"
#include "apriltag/TagFamilyFactory.hpp"

using namespace cv;
using namespace std;
using april::tag::UINT64;
using april::tag::TagFamily;
using april::tag::TagFamilyFactory;
using april::tag::TagDetector;
using april::tag::TagDetection;
using helper::ImageSource;
using helper::GConfig;

struct KEGprocessor : public ImageHelper::ImageSource::Processor {
/////// Vars
	Ptr<keg::AprilTagRecognizer> recognizer;
	keg::Tracker tracker;
	Mat gray;
	bool needToInit;
	bool doCapFrame;
	bool needToCapframe;
	bool videoFromWebcam;
	bool onlyApril;
	double threshKeydistance;
	int framecnt;
	int expMode;
	vector<keg::KeyFrame> keyframes;

/////// Constructor
	KEGprocessor() {
		expMode = 0;
		needToInit = true;
		doCapFrame = true;
		needToCapframe = false;
		videoFromWebcam = false;
		onlyApril = false;
		threshKeydistance = 200;
		framecnt = 0;
	}

	/**
	load multiple templates into the tracker
	TODO currently no detection of multiple id error, user is responsible for this
	*/
	inline void loadTemplateList(vector<string> templatelist)
	{
		const int maxNumKeys = GConfig::Instance().get("keg:maxNumKeys", 100);
		for(int i=0; i<(int)templatelist.size(); ++i)
			tracker.loadTemplate(templatelist[i], maxNumKeys);
		for(int i=0; i<(int)tracker.tdata.size(); ++i) {
			keg::Tracker::TemplateData& td = tracker.tdata[i];
			vector<int> ids;
			vector<Mat> HIs;
			(*recognizer)(td.img, HIs, ids);
			if((int)ids.size() != 1) {
				logle("[loadTemplateList] found none/multiple apriltag"
					" on template image #"<<i<<", erase it.");
				tracker.tdata.erase(tracker.tdata.begin()+i);
			} else {
				td.iHI = HIs.front().inv();
				td.id = ids.front();
				logli("[loadTemplateList] found tag "<<td.id);
			}
		}
	}

/////// Override
	void operator()(cv::Mat& frame) {
		if(frame.empty()) return;
		static helper::PerformanceMeasurer PM(1000);
		PM.tic();

		cvtColor(frame, gray, CV_BGR2GRAY);
		double rms=-1, ncc=-1;
		double camR[3][3]={{1,0,0},{0,1,0},{0,0,1}},camT[3]={0};

		if( needToInit || onlyApril ) {
			needToInit = true;
			logli("[KEGprocessor] INITing...");
			vector<Mat> tmpH;
			vector<int> tmpid;
			(*recognizer)(gray, tmpH, tmpid);
			for(int itr=0; itr<(int)tmpid.size(); ++itr) {
				int id = tmpid[itr];
				bool foundtag=false;
				for(int jtr=0; jtr<(int)tracker.tdata.size(); ++jtr)
					if( foundtag = (id == tracker.tdata[jtr].id) ) break;

				if(foundtag) {
					logli("[April] find tag "<<id);
					Mat initH = tmpH[itr] * tracker.tdata[id].iHI;
					//onlyApril means no refine, just measure quality
					int maxiter = onlyApril?0:20;
					needToInit = !tracker.init(gray, initH, rms, ncc, id, maxiter);
					if(!needToInit) {
						logli("[KEGprocessor] ...INITed");
						tracker.draw3D(frame);
					}
					break;
				}
			}
		} else {
			needToInit=!tracker(gray, rms, ncc, &frame);
		}

		double dur = PM.toc();

		if(!needToInit && expMode==-1) {
			cv::putText( frame,
				string("Currently around location ")
				+helper::num2str(tracker.curT),
				cv::Point(10,30), CV_FONT_NORMAL,
				1, helper::CV_GREEN, 2 );
		} else {
			cv::putText( frame,
				cv::format("FPS=%.2lfHz", 1000./dur),
				cv::Point(10,30), CV_FONT_NORMAL,
				1, helper::CV_GREEN, 2);
		}

#if KEG_DEBUG
		cout<<"process duration="<<dur<<endl;
#endif

		if(!needToInit)
			tracker.GetCameraPose(camR,camT);
		else if(doCapFrame && !videoFromWebcam) {
			for(int i=0; i<3; ++i) {
				camT[i]=numeric_limits<double>::quiet_NaN();
				for(int j=0; j<3; ++j)
					camR[i][j]=numeric_limits<double>::quiet_NaN();
			}
			rms = ncc = numeric_limits<double>::quiet_NaN();
		}
		if (doCapFrame && !videoFromWebcam) {//if file, then save each frame
			keg::CapKeyFrame(keyframes,
				tracker.curT, expMode==-1?Mat():frame, camR, camT, rms, ncc
#if KEG_DEBUG
				, dur
#endif
			);
		}
		++framecnt;
	}//end of operator()

	void handle(char key) {
		switch (key) {
		case '0':
			LogHelper::GLogControl::Instance().level=LogHelper::LOG_QUIET; break;
		case '1':
			LogHelper::GLogControl::Instance().level=LogHelper::LOG_ERROR; break;
		case '2':
			LogHelper::GLogControl::Instance().level=LogHelper::LOG_INFO; break;
		case '3':
			LogHelper::GLogControl::Instance().level=LogHelper::LOG_DEBUG; break;
		case ' ':
			needToInit=true; break;
		case 'c':
			needToCapframe = !needToCapframe;
			if(needToCapframe) logli("[Capture Frame] Begin.");
			else logli("[Capture Frame] End.");
			break;
		}
	}
}; //end of struct KEGprocessor
KEGprocessor processor;

////////////////////////////////////////////////////////////////////////
void usage( int argc, char **argv ) {
	cout<< "[usage] " <<argv[0]<< " <config file>"<<endl;
	cout<< "config file format:\n"<<
		" <url>=\n"
		" <templateFiles>=\n"
		" [experimentMode=0]=\n"
		" [keyframeSavingPath=\"\"]=\n"
		" [apriltagFamilyID=4]=\n" << std::endl;

	cout<< "Supported TagFamily ID List:\n";
	for(int i=0; i<(int)TagFamilyFactory::TAGTOTAL; ++i) {
		cout<<"\t"<<april::tag::TagFamilyFactory_SUPPORT_NAME[i]<<" id="<<i<<endl;
	}
	cout<<"Example ImageSource url:\n";
	cout<<"photo:///home/simbaforrest/Videos/Webcam/seq_UMshort/*\n";
	cout<<"camera://0\n";
	cout<<"video:///home/simbaforrest/Videos/Webcam/keg_april.ogv"<<endl;
	cout<<"Experiment Mode:\n";
	cout<<"0 - A+KEG\n"
		  "1 - A+K G\n"
		  "2 - A+KE \n"
		  "3 - A+K  \n"
		  "4 - A    \n"
		  "5 - A+ E \n"
		  "-1 - APP DEMO"<<endl;
}

int main( int argc, char **argv )
{
	LogHelper::GLogControl::Instance().level=LogHelper::LOG_INFO;
	if (argc > 1) {
		std::string arg1(argv[1]);
		if (arg1 == "-h" || arg1 == "/?" || arg1 == "--help") {
			usage(argc, argv);
			return -1;
		}
	}

	ConfigHelper::Config& cfg = GConfig::Instance();
	if(!cfg.autoLoad("KEG.cfg",DirHelper::getFileDir(argv[0]))) {
		logli("[main] no KEG.cfg file loaded");
	}
	if(argc>1) {
		cfg.reset(argc-1, argv+1);
	}
	logli("[main] final Config:");
	cfg->print(std::cout);
	logli("");
	
	int& experimentMode = processor.expMode;
	std::string keyframeSavingPath;
	int apriltagFamilyID = 4;
	cfg.get("experimentMode", experimentMode);
	cfg.get("keyframeSavingPath",keyframeSavingPath);
	processor.doCapFrame = keyframeSavingPath.empty();
	cfg.get("apriltagFamilyID",apriltagFamilyID);

	//// ImageSource
	cv::Ptr<ImageSource> is = helper::createImageSource(cfg.get("url","camera://0"));
	if(is.empty() || is->done()) {
		tagle("createImageSource failed or no valid imagesource!");
		return -1;
	}
	is->reportInfo();
	processor.videoFromWebcam = false;
	if( is->classname() == "ImageSource_Camera" ) {
		processor.videoFromWebcam = true;
	} else {
		processor.needToCapframe = true;
		processor.needToInit = true;
		is->loop(false);
	}

	//// KEGTracker
	double K[9];
	{
		std::vector<double> K_;
		if(!cfg->exist("K") || 9!=(cfg.getRoot()["K"]>>K_)) {
			logli("[main warn] calibration matrix K"
				" not correctly specified in config!");
			return -1;
		}
		for(int i=0; i<9; ++i) K[i]=K_[i]/K_[8];
	}
	processor.tracker.loadK(K);
	tagli("K matrix loaded:");
	logli(helper::PrintMat<>(3,3,K));

	//// TagDetector
	processor.recognizer = new keg::AprilTagRecognizer;
	Ptr<TagFamily> tagFamily;
	Ptr<TagDetector> detector;
	tagFamily = TagFamilyFactory::create(apriltagFamilyID);
	if(tagFamily.empty()) {
		tagle("create TagFamily fail!");
		return -1;
	}
	std::vector< Ptr<TagFamily> > tagFamilies;
	tagFamilies.push_back(tagFamily);
	detector = new TagDetector(tagFamilies);
	if(detector.empty()) {
		tagle("create TagDetector fail!");
		return -1;
	}
	processor.recognizer->tagFamily = tagFamily;
	processor.recognizer->detector = detector;

	if(!cfg->exist("templateFiles")) {
		tagle("templateFiles not specified in KEG.cfg!");
		return -1;
	}
	tagli("load template image from: ");
	ConfigHelper::ConfigNode &cfgnode = cfg.getRoot();
	cfgnode["templateFiles"].print(std::cout);
	try {
	vector<string> tlist;
	cfgnode["templateFiles"]>>tlist;
	processor.loadTemplateList(tlist);
	} catch(std::exception& e) {
		tagle(e.what());
		return -1;
	} catch(...) {
		tagle(" when loading template image!");
		return -1;
	}

	//// Experiment set up
	switch(experimentMode) {
	case 1://A+K G
		processor.tracker.doEstep(false); break;
	case 2://A+KE
		processor.tracker.doGstep = false; break;
	case 3://A+K
		processor.tracker.doGstep = false;
		processor.tracker.doEstep(false); break;
	case 4://A
		processor.tracker.doKstep = false;
		processor.tracker.doGstep = false;
		processor.tracker.doEstep(false);
		processor.onlyApril=true; break;
	case 5://A+ E
		processor.tracker.doKstep = false;
		processor.tracker.doGstep = false; break;
	case -1://APP DEMO using A+KEG
		processor.expMode = -1; break;
	default:
		processor.expMode = 0;
	}

	//// Main Loop
	is->run(processor,-1);

	//// Finish
	if(processor.doCapFrame) {
		tagli("saving keyframes to: "<<keyframeSavingPath);
		keg::SaveKeyFrames(processor.keyframes,
			keyframeSavingPath, processor.tracker.K,
			processor.expMode!=-1);
	}

	tagli("DONE...exit!");
	return 0;
}
