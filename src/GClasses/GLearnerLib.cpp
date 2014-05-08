/*
  The contents of this file are dedicated by all of its authors, including

    Michael S. Gashler,
    Eric Moyer,
    Michael R. Smith,
    anonymous contributors,

  to the public domain (http://creativecommons.org/publicdomain/zero/1.0/).

  Note that some moral obligations still exist in the absence of legal ones.
  For example, it would still be dishonest to deliberately misrepresent the
  origin of a work. Although we impose no legal requirements to obtain a
  license, it is beseeming for those who build on the works of others to
  give back useful improvements, or pay it forward in their own field. If
  you would like to cite us, a published paper about Waffles can be found
  at http://jmlr.org/papers/volume12/gashler11a/gashler11a.pdf. If you find
  our code to be useful, the Waffles team would love to hear how you use it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include "GLearnerLib.h"
#include <cassert>
#include <time.h>
#include <iostream>
#ifdef WINDOWS
#	include <direct.h>
#	include <process.h>
#endif
#include <exception>
#include <string>
#include <vector>
#include <set>

using namespace GClasses;

using std::cout;
using std::cerr;
using std::string;
using std::vector;
using std::set;
using std::ostringstream;

size_t GLearnerLib::getAttrVal(const char* szString, size_t attrCount)
{
	bool fromRight = false;
	if(*szString == '*')
	{
		fromRight = true;
		szString++;
	}
	if(*szString < '0' || *szString > '9')
		throw Ex("Expected a digit while parsing attribute list");
#ifdef WIN32
	size_t val = (size_t)_strtoui64(szString, (char**)NULL, 10);
#else
	size_t val = strtoull(szString, (char**)NULL, 10);
#endif
	if(fromRight)
		val = attrCount - 1 - val;
	return val;
}

void GLearnerLib::parseAttributeList(vector<size_t>& list, GArgReader& args, size_t attrCount)
{
	const char* szList = args.pop_string();
	set<size_t> attrSet;
	while(true)
	{
		// Skip whitespace
		while(*szList <= ' ' && *szList != '\0')
			szList++;

		// Find the next ',' or the end of string, and
		int i;
		int j = -1;
		for(i = 0; szList[i] != '\0' && szList[i] != ','; i++)
		{
			if(j < 0 && szList[i] == '-')
				j = i;
		}
		if(j >= 0)
		{
			while(szList[j + 1] <= ' ' && szList[j + 1] != '\0')
				j++;
		}

		// Add the attributes to the list
		if(i > 0) // If there is more...
		{
			if(j < 0) // If there is no "-" character in the next value...
			{
				size_t val = getAttrVal(szList, attrCount);
				if(val >= attrCount)
					throw Ex("Invalid column index: ", to_str(val), ". Valid values are from 0 to ", to_str(attrCount - 1), ". (Columns are zero-indexed.)");
				if(attrSet.find(val) != attrSet.end())
					throw Ex("Columns ", to_str(val), " is listed multiple times");
				attrSet.insert(val);
				list.push_back(val);
			}
			else
			{
				size_t beg = getAttrVal(szList, attrCount);
				if(beg >= attrCount)
					throw Ex("Invalid column index: ", to_str(beg), ". Valid values are from 0 to ", to_str(attrCount - 1), ". (Columns are zero-indexed.)");
				size_t end = getAttrVal(szList + j + 1, attrCount);
				if(end >= attrCount)
					throw Ex("Invalid column index: ", to_str(end), ". Valid values are from 0 to ", to_str(attrCount - 1), ". (Columns are zero-indexed.)");
				int step = 1;
				if(end < beg)
					step = -1;
				for(size_t val = beg; true; val += step)
				{
					if(attrSet.find(val) != attrSet.end())
						throw Ex("Column ", to_str(val), " is listed multiple times");
					attrSet.insert(val);
						list.push_back(val);
					if(val == end)
						break;
				}
			}
		}

		// Advance
		szList += i;
		if(*szList == '\0')
			break;
		szList++;
	}
}

void GLearnerLib::loadData(GArgReader& args, Holder<GMatrix>& hFeaturesOut, Holder<GMatrix>& hLabelsOut, bool requireMetadata)
{
	// Load the dataset by extension
	if(args.size() < 1)
		throw Ex("Expected the filename of a datset. (Found end of arguments.)");
	const char* szFilename = args.pop_string();
	PathData pd;
	GFile::parsePath(szFilename, &pd);
	GMatrix data;
	vector<size_t> ambiguousCols;
	if(_stricmp(szFilename + pd.extStart, ".arff") == 0)
		data.loadArff(szFilename);
	else if(_stricmp(szFilename + pd.extStart, ".csv") == 0)
	{
		data.loadCsv(szFilename, ',', false, &ambiguousCols, false);
		if(requireMetadata && !data.relation().areContinuous())
			throw Ex("A data format containing meta-data (such as ARFF) is necessary for this operation.");
	}
	else if(_stricmp(szFilename + pd.extStart, ".dat") == 0)
	{
		data.loadCsv(szFilename, '\0', false, &ambiguousCols, false);
		if(requireMetadata && !data.relation().areContinuous())
			throw Ex("A data format containing meta-data (such as ARFF) is necessary for this operation.");
	}
	else
		throw Ex("Unsupported file format: ", szFilename + pd.extStart);

	// Parse params
	vector<size_t> ignore;
	vector<size_t> labels;
	while(args.next_is_flag())
	{
		if(args.if_pop("-labels"))
			parseAttributeList(labels, args, data.cols());
		else if(args.if_pop("-ignore"))
			parseAttributeList(ignore, args, data.cols());
		else
			throw Ex("Invalid option: ", args.peek());
	}

	// Throw out the ignored attributes
	std::sort(ignore.begin(), ignore.end());
	for(size_t i = ignore.size() - 1; i < ignore.size(); i--)
	{
		for(size_t j = 0; j < ambiguousCols.size(); j++)
		{
			if(ambiguousCols[j] == ignore[i])
			{
				ambiguousCols.erase(ambiguousCols.begin() + j);
				break;
			}
		}

		data.deleteColumn(ignore[i]);
		for(size_t j = 0; j < labels.size(); j++)
		{
			if(labels[j] >= ignore[i])
			{
				if(labels[j] == ignore[i])
					throw Ex("Attribute ", to_str(labels[j]), " is both ignored and used as a label");
				labels[j]--;
			}
		}
	}

	// Swap label columns to the end
	size_t labelDims = std::max((size_t)1, labels.size());
	for(size_t i = 0; i < labels.size(); i++)
	{
		size_t src = labels[i];
		size_t dst = data.cols() - labelDims + i;
		if(src != dst)
		{
			data.swapColumns(src, dst);
			for(size_t j = i + 1; j < labels.size(); j++)
			{
				if(labels[j] == dst)
				{
					labels[j] = src;
					break;
				}
			}
		}
	}

	// Split data into a feature matrix and a label matrix
	GMatrix* pFeatures = data.cloneSub(0, 0, data.rows(), data.cols() - labelDims);
	hFeaturesOut.reset(pFeatures);
	GMatrix* pLabels = data.cloneSub(0, data.cols() - labelDims, data.rows(), labelDims);
	hLabelsOut.reset(pLabels);

	// Report ambiguous cols
	if(ambiguousCols.size() > 0)
	{
		cerr << "WARNING: column";
		if(ambiguousCols.size() > 1)
			cerr << "s";
		cerr << " ";
		for(size_t i = 0; i < ambiguousCols.size(); i++)
		{
			if(i > 0)
			{
				cerr << ", ";
				if(i + 1 == ambiguousCols.size())
					cerr << "and ";
			}
			cerr << to_str(ambiguousCols);
		}
		cerr << " could reasonably be interpreted as either continuous or nominal. Assuming continuous was intended.\n";
	}
}

GAgglomerativeTransducer* GLearnerLib::InstantiateAgglomerativeTransducer(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GAgglomerativeTransducer* pTransducer = new GAgglomerativeTransducer();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pTransducer->autoTune(*pFeatures, *pLabels);
		}
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pTransducer;
}

GBag* GLearnerLib::InstantiateBag(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GBag* pEnsemble = new GBag();
	while(args.size() > 0)
	{
		if(args.if_pop("end"))
			break;
		int instance_count = args.pop_uint();
		int arg_pos = args.get_pos();
		for(int i = 0; i < instance_count; i++)
		{
			args.set_pos(arg_pos);
			GTransducer* pLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
			if(!pLearner->canGeneralize())
			{
				delete(pLearner);
				throw Ex("bag does not support algorithms that cannot generalize.");
			}
			pEnsemble->addLearner((GSupervisedLearner*)pLearner);
		}
	}
	return pEnsemble;
}

GBaselineLearner* GLearnerLib::InstantiateBaseline(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GBaselineLearner* pModel = new GBaselineLearner();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pModel;
}

GBayesianModelAveraging* GLearnerLib::InstantiateBMA(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GBayesianModelAveraging* pEnsemble = new GBayesianModelAveraging();
	while(args.size() > 0)
	{
		if(args.if_pop("end"))
			break;
		int instance_count = args.pop_uint();
		int arg_pos = args.get_pos();
		for(int i = 0; i < instance_count; i++)
		{
			args.set_pos(arg_pos);
			GTransducer* pLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
			if(!pLearner->canGeneralize())
			{
				delete(pLearner);
				throw Ex("BMA does not support algorithms that cannot generalize.");
			}
			pEnsemble->addLearner((GSupervisedLearner*)pLearner);
		}
	}
	return pEnsemble;
}

GBayesianModelCombination* GLearnerLib::InstantiateBMC(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GBayesianModelCombination* pEnsemble = new GBayesianModelCombination();
	size_t samples = 100;
	while(args.next_is_flag())
	{
		if(args.if_pop("-samples"))
			samples = args.pop_uint();
		else
			throw Ex("Invalid option: ", args.peek());
	}
	pEnsemble->setSamples(samples);
	while(args.size() > 0)
	{
		if(args.if_pop("end"))
			break;
		int instance_count = args.pop_uint();
		int arg_pos = args.get_pos();
		for(int i = 0; i < instance_count; i++)
		{
			args.set_pos(arg_pos);
			GTransducer* pLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
			if(!pLearner->canGeneralize())
			{
				delete(pLearner);
				throw Ex("BMC does not support algorithms that cannot generalize.");
			}
			pEnsemble->addLearner((GSupervisedLearner*)pLearner);
		}
	}
	return pEnsemble;
}

GBomb* GLearnerLib::InstantiateBomb(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GBomb* pEnsemble = new GBomb();
	size_t samples = 100;
	while(args.next_is_flag())
	{
		if(args.if_pop("-samples"))
			samples = args.pop_uint();
		else
			throw Ex("Invalid option: ", args.peek());
	}
	pEnsemble->setSamples(samples);
	while(args.size() > 0)
	{
		if(args.if_pop("end"))
			break;
		int instance_count = args.pop_uint();
		int arg_pos = args.get_pos();
		for(int i = 0; i < instance_count; i++)
		{
			args.set_pos(arg_pos);
			GTransducer* pLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
			if(!pLearner->canGeneralize())
			{
				delete(pLearner);
				throw Ex("Bomb does not support algorithms that cannot generalize.");
			}
			pEnsemble->addLearner((GSupervisedLearner*)pLearner);
		}
	}
	return pEnsemble;
}

GResamplingAdaBoost* GLearnerLib::InstantiateBoost(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	double trainingSizeRatio = 1;
	size_t ensembleSize = 30;

	while(args.size() > 0)
	{
		if(args.if_pop("-trainratio"))
		{
			trainingSizeRatio = args.pop_double();
		}
		else if(args.if_pop("-size"))
		{
			ensembleSize = args.pop_uint();
		}
		else
		{
			break;
		}
	}

	GTransducer* pLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
	if(!pLearner->canGeneralize())
	{
		delete(pLearner);
		throw Ex("boost does not support algorithms that cannot generalize.");
	}

	GResamplingAdaBoost* pEnsemble = new GResamplingAdaBoost((GSupervisedLearner*)pLearner, true, new GLearnerLoader());
	pEnsemble->setTrainSize(trainingSizeRatio);
	pEnsemble->setSize(ensembleSize);

	return pEnsemble;
}

GBucket* GLearnerLib::InstantiateBucket(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GBucket* pEnsemble = new GBucket();
	while(args.size() > 0)
	{
		if(args.if_pop("end"))
			break;
		GTransducer* pLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
		if(!pLearner->canGeneralize())
		{
			delete(pLearner);
			throw Ex("crossvalidationselector does not support algorithms that cannot generalize.");
		}
		pEnsemble->addLearner((GSupervisedLearner*)pLearner);
	}
	return pEnsemble;
}

GBucket* GLearnerLib::InstantiateCvdt(GArgReader& args)
{
	size_t trees = args.pop_uint();
	GBucket* pBucket = new GBucket();
	GBag* pBag1 = new GBag();
	pBucket->addLearner(pBag1);
	for(size_t i = 0; i < trees; i++)
		pBag1->addLearner(new GDecisionTree());
	GBag* pBag2 = new GBag();
	pBucket->addLearner(pBag2);
	for(size_t i = 0; i < trees; i++)
		pBag2->addLearner(new GMeanMarginsTree());
	return pBucket;
}

GDecisionTree* GLearnerLib::InstantiateDecisionTree(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GDecisionTree* pModel = new GDecisionTree();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else if(args.if_pop("-binary")){
			pModel->useBinaryDivisions();
		}
		else if(args.if_pop("-random")){
			pModel->useRandomDivisions(args.pop_uint());
		}else if(args.if_pop("-leafthresh")){
			pModel->setLeafThresh(args.pop_uint());
		}else if(args.if_pop("-maxlevels")){
			pModel->setMaxLevels(args.pop_uint());
		}else{
			throw Ex("Invalid option: ", args.peek());
		}
	}
	return pModel;
}

GGaussianProcess* GLearnerLib::InstantiateGaussianProcess(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GGaussianProcess* pModel = new GGaussianProcess();
	while(args.next_is_flag())
	{
		if(args.if_pop("-noise")){
			pModel->setNoiseVariance(args.pop_double());
		}else if(args.if_pop("-prior")){
			pModel->setWeightsPriorVariance(args.pop_double());
		}else if(args.if_pop("-maxsamples")){
			pModel->setMaxSamples(args.pop_uint());
		}else if(args.if_pop("-kernel")){
			if(args.if_pop("identity"))
				pModel->setKernel(new GKernelIdentity());
			else if(args.if_pop("chisquared"))
				pModel->setKernel(new GKernelChiSquared());
			else if(args.if_pop("rbf"))
				pModel->setKernel(new GKernelGaussianRBF(args.pop_double()));
			else if(args.if_pop("polynomial"))
				pModel->setKernel(new GKernelPolynomial(args.pop_double(), args.pop_uint()));
			else throw Ex("Unrecognized kernel: ", args.pop_string());
		}else{
			throw Ex("Invalid option: ", args.peek());
		}
	}
	return pModel;
}

GGraphCutTransducer* GLearnerLib::InstantiateGraphCutTransducer(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GGraphCutTransducer* pTransducer = new GGraphCutTransducer();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pTransducer->autoTune(*pFeatures, *pLabels);
		}
		else if(args.if_pop("-neighbors"))
			pTransducer->setNeighbors(args.pop_uint());
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pTransducer;
}

GBayesianModelCombination* GLearnerLib::InstantiateHodgePodge(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GBayesianModelCombination* pEnsemble = new GBayesianModelCombination();

	GNaiveBayes* pNB = new GNaiveBayes();
	pEnsemble->addLearner(pNB);

	GNaiveBayes* pNB2 = new GNaiveBayes();
	pNB2->setEquivalentSampleSize(1.0);
	pEnsemble->addLearner(pNB2);

	GLinearRegressor* pLin = new GLinearRegressor();
	pEnsemble->addLearner(pLin);

	GNaiveInstance* pNI = new GNaiveInstance();
	pEnsemble->addLearner(pNI);

	GNeuralNet* pNN = new GNeuralNet();
	pEnsemble->addLearner(pNN);

	GBaselineLearner* pBL = new GBaselineLearner();
	pEnsemble->addLearner(pBL);

	for(size_t i = 0; i < 2; i++)
	{
		GReservoirNet* pRN = new GReservoirNet();
		pEnsemble->addLearner(pRN);
	}

	for(size_t i = 0; i < 6; i++)
	{
		GDecisionTree* pDT = new GDecisionTree();
		pDT->setLeafThresh(6 * i);
		pEnsemble->addLearner(pDT);
	}

	for(size_t i = 0; i < 12; i++)
	{
		GDecisionTree* pRDT = new GDecisionTree();
		pRDT->useRandomDivisions(1);
		pEnsemble->addLearner(pRDT);
	}

	for(size_t i = 0; i < 8; i++)
	{
		GMeanMarginsTree* pMM = new GMeanMarginsTree();
		pEnsemble->addLearner(pMM);
	}

	for(size_t i = 0; i < 5; i++)
	{
		GKNN* pKnn = new GKNN();
		pKnn->setNeighborCount(1);
		pKnn->drawRandom(16);
		pEnsemble->addLearner(pKnn);
	}

	for(size_t i = 0; i < 3; i++)
	{
		GKNN* pKnn = new GKNN();
		pKnn->setNeighborCount(3);
		pKnn->drawRandom(24);
		pEnsemble->addLearner(pKnn);
	}

	return pEnsemble;
}

GKNN* GLearnerLib::InstantiateKNN(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GKNN* pModel = new GKNN();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else if(args.if_pop("-nonormalize"))
			pModel->setNormalizeScaleFactors(false);
		else if(args.if_pop("-neighbors"))
			pModel->setNeighborCount(args.pop_uint());
		else if(args.if_pop("-equalweight"))
			pModel->setInterpolationMethod(GKNN::Mean);
		else if(args.if_pop("-scalefeatures"))
			pModel->setOptimizeScaleFactors(true);
		else if(args.if_pop("-cosine"))
			pModel->setMetric(new GCosineSimilarity(), true);
		else if(args.if_pop("-pearson"))
			pModel->setMetric(new GPearsonCorrelation(), true);
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pModel;
}

GLinearRegressor* GLearnerLib::InstantiateLinearRegressor(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GLinearRegressor* pModel = new GLinearRegressor();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pModel;
}

GMeanMarginsTree* GLearnerLib::InstantiateMeanMarginsTree(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GMeanMarginsTree* pModel = new GMeanMarginsTree();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pModel;
}

GNaiveBayes* GLearnerLib::InstantiateNaiveBayes(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GNaiveBayes* pModel = new GNaiveBayes();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else if(args.if_pop("-ess"))
			pModel->setEquivalentSampleSize(args.pop_double());
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pModel;
}

GNaiveInstance* GLearnerLib::InstantiateNaiveInstance(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GNaiveInstance* pModel = new GNaiveInstance();
	while(args.next_is_flag())
	{
		if(args.if_pop("-"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else if(args.if_pop("-neighbors"))
			pModel->setNeighbors(args.pop_uint());
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pModel;
}

GNeighborTransducer* GLearnerLib::InstantiateNeighborTransducer(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GNeighborTransducer* pTransducer = new GNeighborTransducer();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pTransducer->autoTune(*pFeatures, *pLabels);
		}
		else if(args.if_pop("-neighbors"))
			pTransducer->setNeighbors(args.pop_uint());
		else
			throw Ex("Invalid option: ", args.peek());
	}
	return pTransducer;
}

GNeuralNet* GLearnerLib::InstantiateNeuralNet(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GNeuralNet* pModel = new GNeuralNet();
	while(args.next_is_flag())
	{
		if(args.if_pop("-autotune"))
		{
			if(!pFeatures || !pLabels)
				throw Ex("Insufficient data to support automatic tuning");
			pModel->autoTune(*pFeatures, *pLabels);
		}
		else if(args.if_pop("-addlayer"))
			pModel->addLayer(new GLayerClassic(FLEXIBLE_SIZE, args.pop_uint()));
		else if(args.if_pop("-learningrate"))
			pModel->setLearningRate(args.pop_double());
		else if(args.if_pop("-momentum"))
			pModel->setMomentum(args.pop_double());
		else if(args.if_pop("-windowepochs"))
			pModel->setWindowSize(args.pop_uint());
		else if(args.if_pop("-minwindowimprovement"))
			pModel->setImprovementThresh(args.pop_double());
		else if(args.if_pop("-holdout"))
			pModel->setValidationPortion(args.pop_double());
/*		else if(args.if_pop("-activation"))
		{
			const char* szSF = args.pop_string();
			GActivationFunction* pSF = NULL;
			if(strcmp(szSF, "logistic") == 0)
				pSF = new GActivationLogistic();
			else if(strcmp(szSF, "arctan") == 0)
				pSF = new GActivationArcTan();
			else if(strcmp(szSF, "tanh") == 0)
				pSF = new GActivationTanH();
			else if(strcmp(szSF, "algebraic") == 0)
				pSF = new GActivationAlgebraic();
			else if(strcmp(szSF, "identity") == 0)
				pSF = new GActivationIdentity();
			else if(strcmp(szSF, "gaussian") == 0)
				pSF = new GActivationGaussian();
			else if(strcmp(szSF, "sinc") == 0)
				pSF = new GActivationSinc();
			else if(strcmp(szSF, "bend") == 0)
				pSF = new GActivationBend();
			else if(strcmp(szSF, "bidir") == 0)
				pSF = new GActivationBiDir();
			else if(strcmp(szSF, "piecewise") == 0)
				pSF = new GActivationPiecewise();
			else
				throw Ex("Unrecognized activation function: ", szSF);
			pModel->setActivationFunction(pSF, true);
		}*/
		else
			throw Ex("Invalid option: ", args.peek());
	}
	pModel->addLayer(new GLayerClassic(FLEXIBLE_SIZE, FLEXIBLE_SIZE));
	return pModel;
}

GRandomForest* GLearnerLib::InstantiateRandomForest(GArgReader& args)
{
	size_t trees = args.pop_uint();
	size_t samples = 1;
	while(args.next_is_flag())
	{
		if(args.if_pop("-samples"))
			samples = args.pop_uint();
		else
			throw Ex("Invalid random forest option: ", args.peek());
	}
	return new GRandomForest(trees, samples);
}

GReservoirNet* GLearnerLib::InstantiateReservoirNet(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GReservoirNet* pModel = new GReservoirNet();
	while(args.next_is_flag())
	{
		if(args.if_pop("-augments")){
			pModel->setAugments(args.pop_uint());
		}else if(args.if_pop("-deviation")){
			pModel->setWeightDeviation(args.pop_double());
		}else if(args.if_pop("-layers")){
			pModel->setReservoirLayers(args.pop_uint());
		}else{
			throw Ex("Invalid option: ", args.peek());
		}
	}
	return pModel;
}

GWag* GLearnerLib::InstantiateWag(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	GWag* pWag = new GWag(0);
	GNeuralNet* pModel = pWag->model();
	size_t modelCount = 10;
	while(args.next_is_flag())
	{
		if(args.if_pop("-noalign"))
			pWag->noAlign();
		else if(args.if_pop("-addlayer"))
			pModel->addLayer(new GLayerClassic(FLEXIBLE_SIZE, args.pop_uint()));
		else if(args.if_pop("-learningrate"))
			pModel->setLearningRate(args.pop_double());
		else if(args.if_pop("-momentum"))
			pModel->setMomentum(args.pop_double());
		else if(args.if_pop("-models"))
			modelCount = args.pop_uint();
		else if(args.if_pop("-windowepochs"))
			pModel->setWindowSize(args.pop_uint());
		else if(args.if_pop("-minwindowimprovement"))
			pModel->setImprovementThresh(args.pop_double());
/*		else if(args.if_pop("-activation"))
		{
			const char* szSF = args.pop_string();
			GActivationFunction* pSF = NULL;
			if(strcmp(szSF, "logistic") == 0)
				pSF = new GActivationLogistic();
			else if(strcmp(szSF, "arctan") == 0)
				pSF = new GActivationArcTan();
			else if(strcmp(szSF, "tanh") == 0)
				pSF = new GActivationTanH();
			else if(strcmp(szSF, "algebraic") == 0)
				pSF = new GActivationAlgebraic();
			else if(strcmp(szSF, "identity") == 0)
				pSF = new GActivationIdentity();
			else if(strcmp(szSF, "gaussian") == 0)
				pSF = new GActivationGaussian();
			else if(strcmp(szSF, "sinc") == 0)
				pSF = new GActivationSinc();
			else if(strcmp(szSF, "bend") == 0)
				pSF = new GActivationBend();
			else if(strcmp(szSF, "bidir") == 0)
				pSF = new GActivationBiDir();
			else if(strcmp(szSF, "piecewise") == 0)
				pSF = new GActivationPiecewise();
			else
				throw Ex("Unrecognized activation function: ", szSF);
			pModel->setActivationFunction(pSF, true);
		}*/
		else
			throw Ex("Invalid option: ", args.peek());
	}
	pModel->addLayer(new GLayerClassic(FLEXIBLE_SIZE, FLEXIBLE_SIZE));
	pWag->setModelCount(modelCount);
	return pWag;
}

void GLearnerLib::showInstantiateAlgorithmError(const char* szMessage, GArgReader& args)
{
	cerr << "_________________________________\n";
	cerr << szMessage << "\n\n";
	const char* szAlgName = args.peek();
	UsageNode* pAlgTree = makeAlgorithmUsageTree();
	Holder<UsageNode> hAlgTree(pAlgTree);
	if(szAlgName)
	{
		UsageNode* pUsageAlg = pAlgTree->choice(szAlgName);
		if(pUsageAlg)
		{
			cerr << "Partial Usage Information:\n\n";
			pUsageAlg->print(cerr, 0, 3, 76, 1000, true);
		}
		else
		{
			cerr << "\"" << szAlgName << "\" is not a recognized algorithm. Try one of these:\n\n";
			pAlgTree->print(cerr, 0, 3, 76, 1, false);
		}
	}
	else
	{
		cerr << "Expected an algorithm. Here are some choices:\n";
		pAlgTree->print(cerr, 0, 3, 76, 1, false);
	}
	cerr << "\nTo see full usage information, run:\n	waffles_learn usage\n\n";
	cerr << "For a graphical tool that will help you to build a command, run:\n	waffles_wizard\n";
	cerr.flush();
}

GTransducer* GLearnerLib::InstantiateAlgorithm(GArgReader& args, GMatrix* pFeatures, GMatrix* pLabels)
{
	int argPos = args.get_pos();
	if(args.size() < 1)
		throw Ex("No algorithm specified.");
	GTransducer* pAlg = NULL;
	try
	{
		if(args.if_pop("agglomerativetransducer"))
			pAlg = InstantiateAgglomerativeTransducer(args, pFeatures, pLabels);
		else if(args.if_pop("bag"))
			pAlg = InstantiateBag(args, pFeatures, pLabels);
		else if(args.if_pop("baseline"))
			pAlg = InstantiateBaseline(args, pFeatures, pLabels);
		else if(args.if_pop("bma"))
			pAlg = InstantiateBMA(args, pFeatures, pLabels);
		else if(args.if_pop("bmc"))
			pAlg = InstantiateBMC(args, pFeatures, pLabels);
		else if(args.if_pop("bomb"))
			pAlg = InstantiateBomb(args, pFeatures, pLabels);
		else if(args.if_pop("boost"))
			pAlg = InstantiateBoost(args, pFeatures, pLabels);
		else if(args.if_pop("bucket"))
			pAlg = InstantiateBucket(args, pFeatures, pLabels);
		else if(args.if_pop("cvdt"))
			pAlg = InstantiateCvdt(args);
		else if(args.if_pop("decisiontree"))
			pAlg = InstantiateDecisionTree(args, pFeatures, pLabels);
		else if(args.if_pop("gaussianprocess"))
			pAlg = InstantiateGaussianProcess(args, pFeatures, pLabels);
		else if(args.if_pop("graphcuttransducer"))
			pAlg = InstantiateGraphCutTransducer(args, pFeatures, pLabels);
		else if(args.if_pop("hodgepodge"))
			pAlg = InstantiateHodgePodge(args, pFeatures, pLabels);
		else if(args.if_pop("knn"))
			pAlg = InstantiateKNN(args, pFeatures, pLabels);
		else if(args.if_pop("linear"))
			pAlg = InstantiateLinearRegressor(args, pFeatures, pLabels);
		else if(args.if_pop("meanmarginstree"))
			pAlg = InstantiateMeanMarginsTree(args, pFeatures, pLabels);
		else if(args.if_pop("naivebayes"))
			pAlg = InstantiateNaiveBayes(args, pFeatures, pLabels);
		else if(args.if_pop("naiveinstance"))
			pAlg = InstantiateNaiveInstance(args, pFeatures, pLabels);
		else if(args.if_pop("neighbortransducer"))
			pAlg = InstantiateNeighborTransducer(args, pFeatures, pLabels);
		else if(args.if_pop("neuralnet"))
			pAlg = InstantiateNeuralNet(args, pFeatures, pLabels);
		else if(args.if_pop("randomforest"))
			pAlg = InstantiateRandomForest(args);
		else if(args.if_pop("reservoir"))
			pAlg = InstantiateReservoirNet(args, pFeatures, pLabels);
		else if(args.if_pop("wag"))
			pAlg = InstantiateWag(args, pFeatures, pLabels);
		else
			throw Ex("Unrecognized algorithm name: ", args.peek());
	}
	catch(const std::exception& e)
	{
		args.set_pos(argPos);
		if(strcmp(e.what(), "nevermind") != 0) // if an error message was not already displayed...
			showInstantiateAlgorithmError(e.what(), args);
		throw Ex("nevermind"); // this means "don't display another error message"
	}
	if(pAlg->canGeneralize())
	{
		GSupervisedLearner* pLearner = (GSupervisedLearner*)pAlg;
		pAlg = new GAutoFilter(pLearner);
	}
	return pAlg;
}

void GLearnerLib::autoTuneDecisionTree(GMatrix& features, GMatrix& labels)
{
	GDecisionTree dt;
	dt.autoTune(features, labels);
	cout << "decisiontree";
	if(dt.leafThresh() != 1)
		cout << " -leafthresh " << dt.leafThresh();
	if(dt.isBinary())
		cout << " -binary";
	cout << "\n";
}

void GLearnerLib::autoTuneKNN(GMatrix& features, GMatrix& labels)
{
	GKNN model;
	model.autoTune(features, labels);
	cout << "knn";
	if(model.neighborCount() != 1)
		cout << " -neighbors " << model.neighborCount();
	cout << "\n";
}

void GLearnerLib::autoTuneNeuralNet(GMatrix& features, GMatrix& labels)
{
	cout << "Warning: Because neural nets take a long time to train, it could take hours to train with enough parameter variations to determine with confidence which parameters are best. (If possible, I would strongly advise running this as a background process while you do something else, rather than sit around waiting for it to finish.)";
	cout.flush();
	GNeuralNet nn;
	nn.autoTune(features, labels);
//	const char* szCurrent = "logistic";
	cout << "neuralnet";
	for(size_t i = 0; i < nn.layerCount(); i++)
	{
/*		const char* szActivationName = nn.layer(i).m_pActivationFunction->name();
		if(strcmp(szActivationName, szCurrent) != 0)
		{
			cout << " -activation " << szActivationName;
			szCurrent = szActivationName;
		}*/
		if(i < nn.layerCount() - 1)
			cout << " -addlayer " << nn.layer(i).outputs();
	}
	if(nn.momentum() > 0.0)
		cout << " -momentum " << nn.momentum();
	cout << "\n";
}

void GLearnerLib::autoTuneNaiveBayes(GMatrix& features, GMatrix& labels)
{
	GNaiveBayes model;
	model.autoTune(features, labels);
	cout << "naivebayes";
	cout << " -ess " << model.equivalentSampleSize();
	cout << "\n";
}

void GLearnerLib::autoTuneNaiveInstance(GMatrix& features, GMatrix& labels)
{
	GNaiveInstance model;
	model.autoTune(features, labels);
	cout << "naiveinstance";
	cout << " -neighbors " << model.neighbors();
	cout << "\n";
}

void GLearnerLib::autoTuneGraphCutTransducer(GMatrix& features, GMatrix& labels)
{
	GGraphCutTransducer transducer;
	transducer.autoTune(features, labels);
	cout << "graphcuttransducer";
	cout << " -neighbors " << transducer.neighbors();
	cout << "\n";
}

void GLearnerLib::autoTune(GArgReader& args)
{
	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();

	// Load the model name
	const char* szModel = args.pop_string();
	if(strcmp(szModel, "agglomerativetransducer") == 0)
		cout << "agglomerativetransducer\n"; // no params to tune
	else if(strcmp(szModel, "decisiontree") == 0)
		autoTuneDecisionTree(*pFeatures, *pLabels);
	else if(strcmp(szModel, "graphcuttransducer") == 0)
		autoTuneGraphCutTransducer(*pFeatures, *pLabels);
	else if(strcmp(szModel, "knn") == 0)
		autoTuneKNN(*pFeatures, *pLabels);
	else if(strcmp(szModel, "meanmarginstree") == 0)
		cout << "meanmarginstree\n"; // no params to tune
	else if(strcmp(szModel, "neuralnet") == 0)
		autoTuneNeuralNet(*pFeatures, *pLabels);
	else if(strcmp(szModel, "naivebayes") == 0)
		autoTuneNaiveBayes(*pFeatures, *pLabels);
	else if(strcmp(szModel, "naiveinstance") == 0)
		autoTuneNaiveInstance(*pFeatures, *pLabels);
	else
		throw Ex("Sorry, autotune does not currently support a model named ", szModel, ".");
}

void GLearnerLib::Train(GArgReader& args)
{
	// Parse options
	size_t seed = getpid() * (unsigned int)time(NULL);
	bool calibrate = false;
	bool embed = false;
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else if(args.if_pop("-calibrate"))
			calibrate = true;
		else if(args.if_pop("-embed"))
			embed = true;
		else
			throw Ex("Invalid train option: ", args.peek());
	}

	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();

	// Instantiate the modeler
	GTransducer* pSupLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
	pSupLearner->rand().setSeed(seed);
	Holder<GTransducer> hModel(pSupLearner);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());
	if(!pSupLearner->canGeneralize())
		throw Ex("This algorithm cannot be \"trained\". It can only be used to \"transduce\".");
	GSupervisedLearner* pModel = (GSupervisedLearner*)pSupLearner;
	if(calibrate)
	{
		GCalibrator* pCal = new GCalibrator(pModel);
		pModel = pCal;
		hModel.reset(pCal);
	}

	// Train the modeler
	pModel->train(*pFeatures, *pLabels);

	// Output the trained model
	GDom doc;
	GDomNode* pRoot = pModel->serialize(&doc);
	doc.setRoot(pRoot);
	if(embed)
		doc.writeJsonCpp(cout);
	else
		doc.writeJson(cout);
}

void GLearnerLib::predict(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else
			throw Ex("Invalid predict option: ", args.peek());
	}

	// Load the model
	GDom doc;
	if(args.size() < 1)
		throw Ex("Model not specified.");
	doc.loadJson(args.pop_string());
	GLearnerLoader ll(true);
	GSupervisedLearner* pModeler = ll.loadLearner(doc.root());
	Holder<GSupervisedLearner> hModeler(pModeler);
	pModeler->rand().setSeed(seed);

	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels, true);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();
	if(pLabels->cols() != pModeler->relLabels().size())
		throw Ex("The model was trained with ", to_str(pModeler->relLabels().size()), " label dims, but the specified dataset has ", to_str(pLabels->cols()));
	if(!pFeatures->relation().isCompatible(pModeler->relFeatures()) || !pLabels->relation().isCompatible(pModeler->relLabels()))
		throw Ex("This data is not compatible with the data that was used to train the model. (The column meta-data is different.)");
	pLabels->setAll(0.0); // Wipe out the existing labels, just to be absolutely certain that we don't somehow accidentally let them influence the predictions

	// Test
	for(size_t i = 0; i < pFeatures->rows(); i++)
	{
		double* pFeatureVec = pFeatures->row(i);
		double* pLabelVec = pLabels->row(i);
		pModeler->predict(pFeatureVec, pLabelVec);
	}

	// Print results
	pLabels->print(cout);
}

void GLearnerLib::predictDistribution(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else
			throw Ex("Invalid option: ", args.peek());
	}

	// Load the model
	GDom doc;
	if(args.size() < 1)
		throw Ex("Model not specified.");
	doc.loadJson(args.pop_string());
	GLearnerLoader ll(true);
	GSupervisedLearner* pModeler = ll.loadLearner(doc.root());
	Holder<GSupervisedLearner> hModeler(pModeler);
	pModeler->rand().setSeed(seed);

	// Parse the pattern
	if(pModeler->relFeatures().type() != GRelation::ARFF)
		throw Ex("meta data is missing");
	GArffRelation* pFeatureRel = (GArffRelation*)&pModeler->relFeatures();
	if(pModeler->relLabels().type() != GRelation::ARFF)
		throw Ex("meta data is missing");
	GArffRelation* pLabelRel = (GArffRelation*)&pModeler->relLabels();
	size_t featureDims = pModeler->relFeatures().size();
	GTEMPBUF(double, pattern, featureDims);
	for(size_t i = 0; i < featureDims; i++)
		pattern[i] = pFeatureRel->parseValue(i, args.pop_string());

	// Predict
	GPrediction* out = new GPrediction[pLabelRel->size()];
	ArrayHolder<GPrediction> hOut(out);
	bool gotPrediction = false;
	try
	{
		pModeler->predictDistribution(pattern, out);
		gotPrediction = true;
	}
	catch(const std::exception& e)
	{
		cout << e.what() << "\n";
	}

	if(gotPrediction)
	{
		// Display the prediction
		cout.precision(8);
		for(size_t i = 0; i < pLabelRel->size(); i++)
		{
			if(i > 0)
				cout << ", ";
			if(pLabelRel->valueCount(i) == 0)
				cout << out[i].mode();
			else
				pLabelRel->printAttrValue(cout, i, (int)out[i].mode());
		}
		cout << "\n\n";

		// Display the distribution
		for(size_t i = 0; i < pLabelRel->size(); i++)
		{
			if(out[i].isContinuous())
			{
				GNormalDistribution* pNorm = out[i].asNormal();
				cout << pLabelRel->attrName(i) << ") Normal: predicted mean=" << pNorm->mean() << " predicted variance=" << pNorm->variance() << "\n";
			}
			else
			{
				GCategoricalDistribution* pCat = out[i].asCategorical();
				cout << pLabelRel->attrName(i) << ") Categorical confidences: {";
				double* pValues = pCat->values(pCat->valueCount());
				for(size_t j = 0; j < pCat->valueCount(); j++)
				{
					if(j > 0)
						cout << ", ";
					pLabelRel->printAttrValue(cout, i, (int)j);
					cout << "=" << pValues[j];
				}
				cout << "}\n";
			}
		}
	}
	else
	{
		// This model apparently cannot predict distributions, so let's just predict normally
		double* out2 = new double[pLabelRel->size()];
		ArrayHolder<double> hOut2(out2);
		pModeler->predict(pattern, out2);

		// Display the prediction
		cout.precision(8);
		for(size_t i = 0; i < pLabelRel->size(); i++)
		{
			if(i > 0)
				cout << ", ";
			if(pLabelRel->valueCount(i) == 0)
				cout << out2[i];
			else
				pLabelRel->printAttrValue(cout, i, (int)out2[i]);
		}
		cout << "\n\n";
	}
}

void GLearnerLib::leftJustifiedString(const char* pIn, char* pOut, size_t outLen)
{
	size_t inLen = std::min(outLen, strlen(pIn));
	memcpy(pOut, pIn, inLen);
	memset(pOut + inLen, ' ', outLen - inLen);
	pOut[outLen] = '\0';
}

void GLearnerLib::rightJustifiedString(const char* pIn, char* pOut, size_t outLen)
{
	size_t inLen = strlen(pIn);
	size_t spaces = std::max(outLen, inLen) - inLen;
	memset(pOut, ' ', spaces);
	memcpy(pOut + spaces, pIn, outLen - spaces);
	pOut[outLen] = '\0';
}

///\brief Returns the header for the machine readable confusion matrix
///for variable \a variable_idx as printed by
///printMachineReadableConfusionMatrices
///
///The header is comma-separated values. The first two entries in the
///header are "Variable Name","Variable Index". The rest of the
///entries fit the format "Expected:xxx/Got:yyy" where xxx and yyy are
///two values that the variable can take on.
///
///\param variable_idx the index of the variable in the relation
///
///\param pRelation a pointer to the relation from which the
///                 variable_idx-'th variable is taken. Cannot be null
std::string GLearnerLib::machineReadableConfusionHeader(std::size_t variable_idx, const GRelation* pRelation){
  assert(pRelation != NULL);
  ostringstream out;
  out << "\"Variable Name\",\"Variable Index\"";
  std::size_t n = pRelation->valueCount(variable_idx);
  for(std::size_t r = 0; r < n; ++r){
    std::ostringstream expected_name;
    pRelation->printAttrValue(expected_name, variable_idx, r);
    std::string e = expected_name.str();
    for(std::size_t c = 0; c < n; ++c){
      std::ostringstream got_name;
      pRelation->printAttrValue(got_name, variable_idx, c);
      std::string g = got_name.str();
      out << ",\"Expected:" << e <<" Got:" << g << "\"";
    }
  }
  return out.str();
}

//\brief Returns the data for the machine readable confusion matrix
///for variable \a variable_idx as printed by
///printMachineReadableConfusionMatrices
///
///The first entry is the name of the variable. The second entry is
///the value of variable_idx, The entry (r*numCols+c)+2 where r and c are both in 0..nv-1, nv being the number of values that the variable takes on, is the entry at row r and column c of *pMatrix
///
///\param variable_idx the index of the variable in the relation
///
///\param pRelation a pointer to the relation from which the
///                 variable_idx-'th variable is taken. Cannot be NULL.
///
///\param pMatrix a pointer to the confusion matrix. (*pMatrix)[r][c]
///               is the number of times that r was expected and c was
///               received. Cannot be NULL.
std::string GLearnerLib::machineReadableConfusionData(std::size_t variable_idx, const GRelation* pRelation, GMatrix const * const pMatrix){
  ostringstream out;
  {
    ostringstream v_name;
    pRelation->printAttrName(v_name, variable_idx);
    out << v_name.str() << "," << variable_idx;
  }
  const GMatrix& m = *pMatrix;
  std::size_t n = pRelation->valueCount(variable_idx);
  for(std::size_t r = 0; r < n; ++r){
    for(std::size_t c = 0; c < n; ++c){
      out << "," << m[r][c];
    }
  }
  return out.str();
}

///\brief Prints the confusion matrices as machine-readable csv-like lines.
///
///The first line is a header giving the names of the columns for the
///next line.  The first column is the name of the label variable for
///which the matrix is being printed.  The rest of the columns are the
///names of the expected/got values (row/column in the input matrices)
///
///\param pRelation the relation for which the confusion matrices are
///                 given.  Cannot be NULL.
///
///\param matrixArray matrixArray[i] is null if there is no matrix to
///                   be printed. Otherwise matrixArray[i] is the
///                   confusion matrix for the i'th attribute of
///                   pRelation. Row r, column c of matrixArray[i] is the
///                   number of times the value r of the attribute was expected
///                   and c was encountered.
void GLearnerLib::printMachineReadableConfusionMatrices(const GRelation* pRelation, vector<GMatrix*>& matrixArray){
  for(size_t i = 0; i < matrixArray.size(); i++){
    if(matrixArray[i] == NULL){
      continue;
    }
    std::cout << machineReadableConfusionHeader(i, pRelation) << std::endl;
    std::cout << machineReadableConfusionData(i, pRelation, matrixArray[i])
	      << std::endl;
  }
}

void GLearnerLib::printConfusionMatrices(const GRelation* pRelation, vector<GMatrix*>& matrixArray)
{
	cout << "\n(Rows=expected values, Cols=predicted values, Elements=number of occurrences)\n\n";
	char buf[41];
	ostringstream oss;
	for(size_t i = 0; i < matrixArray.size(); i++)
	{
		if(matrixArray[i] == NULL)
			continue;
		GMatrix& cm = *matrixArray[i];

		// Print attribute name
		oss.precision(9);
		oss.str("");
		oss << "Confusion matrix for ";
		pRelation->printAttrName(oss, i);
		string s = oss.str();
		leftJustifiedString(s.c_str(), buf, 40);
		cout << buf;

		// Print column numbers
		for(size_t j = 0; j < cm.cols(); j++)
		{
			oss.str("");
			pRelation->printAttrValue(oss, i, (double)j);
			s = oss.str();
			rightJustifiedString(s.c_str(), buf, 12);
			cout << buf;
		}
		cout << "\n";

		// Print the confusion matrix values
		for(size_t k = 0; k < cm.rows(); k++)
		{
			oss.str("");
			pRelation->printAttrValue(oss, i, (double)k);
			s = oss.str();
			rightJustifiedString(s.c_str(), buf, 40);
			cout << buf;
			for(size_t j = 0; j < cm.cols(); j++)
			{
				oss.str("");
				oss << cm[k][j];
				s = oss.str();
				rightJustifiedString(s.c_str(), buf, 12);
				cout << buf;
			}
			cout << "\n";
		}
		cout << "\n";
	}
}

void GLearnerLib::Test(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	bool confusion = false;
	bool confusioncsv = false;
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else if(args.if_pop("-confusion"))
			confusion = true;
		else if(args.if_pop("-confusioncsv"))
			confusioncsv = true;
		else
			throw Ex("Invalid test option: ", args.peek());
	}

	// Load the model
	GDom doc;
	if(args.size() < 1)
		throw Ex("Model not specified.");
	doc.loadJson(args.pop_string());
	GLearnerLoader ll(true);
	GSupervisedLearner* pModeler = ll.loadLearner(doc.root());
	Holder<GSupervisedLearner> hModeler(pModeler);
	pModeler->rand().setSeed(seed);

	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels, true);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();
	if(!pLabels->relation().isCompatible(pModeler->relLabels()))
		throw Ex("This dataset is not compatible with the one used to train the model. (The meta-data is different.)");

	// Test
	double mse = pModeler->sumSquaredError(*pFeatures, *pLabels) / pFeatures->rows();
	cout << "Mean squared error: " << to_str(mse) << "\n";

	if(confusion || confusioncsv)
	{
		vector<GMatrix*> confusionMatrices;
		pModeler->confusion(*pFeatures, *pLabels, confusionMatrices);

		// Print the confusion matrix
		if(confusion){
			printConfusionMatrices(&pLabels->relation(), confusionMatrices);
		}

		if(confusioncsv){
			printMachineReadableConfusionMatrices(&pLabels->relation(), confusionMatrices);
		}
	}
}

void GLearnerLib::Transduce(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else
			throw Ex("Invalid transduce option: ", args.peek());
	}

	// Load the data sets
	if(args.size() < 1)
		throw Ex("No labeled set specified.");

	// Load the labeled and unlabeled sets
	Holder<GMatrix> hFeatures1, hLabels1, hFeatures2, hLabels2;
	loadData(args, hFeatures1, hLabels1, true);
	loadData(args, hFeatures2, hLabels2, true);
	GMatrix* pFeatures1 = hFeatures1.get();
	GMatrix* pLabels1 = hLabels1.get();
	GMatrix* pFeatures2 = hFeatures2.get();
	GMatrix* pLabels2 = hLabels2.get();
	if(pFeatures1->cols() != pFeatures2->cols() || pLabels1->cols() != pLabels2->cols())
		throw Ex("The labeled and unlabeled datasets must have the same number of columns. (The labels in the unlabeled set are just place-holders, and will be overwritten.)");

	// Instantiate the modeler
	GTransducer* pSupLearner = InstantiateAlgorithm(args, pFeatures1, pLabels1);
	Holder<GTransducer> hModel(pSupLearner);
	pSupLearner->rand().setSeed(seed);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());

	// Transduce
	GMatrix* pLabels3 = pSupLearner->transduce(*pFeatures1, *pLabels1, *pFeatures2);
	Holder<GMatrix> hLabels3(pLabels3);

	// Print results
	pLabels3->print(cout);
}

void GLearnerLib::TransductiveAccuracy(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
//	bool confusion = false;
//	bool confusioncsv = false;
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
/*		else if(args.if_pop("-confusion"))
			confusion = true;
		else if(args.if_pop("-confusioncsv"))
			confusioncsv = true;*/
		else
			throw Ex("Invalid transacc option: ", args.peek());
	}

	// Load the data sets
	Holder<GMatrix> hFeatures1, hLabels1, hFeatures2, hLabels2;
	loadData(args, hFeatures1, hLabels1, true);
	loadData(args, hFeatures2, hLabels2, true);
	GMatrix* pFeatures1 = hFeatures1.get();
	GMatrix* pLabels1 = hLabels1.get();
	GMatrix* pFeatures2 = hFeatures2.get();
	GMatrix* pLabels2 = hLabels2.get();
	if(pFeatures1->cols() != pFeatures2->cols() || pLabels1->cols() != pLabels2->cols())
		throw Ex("The training and test datasets must have the same number of columns.");

	// Instantiate the modeler
	GTransducer* pSupLearner = InstantiateAlgorithm(args, pFeatures1, pLabels1);
	Holder<GTransducer> hModel(pSupLearner);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());
	pSupLearner->rand().setSeed(seed);

	// Transduce and measure accuracy
	GTEMPBUF(double, results, pLabels1->cols());
	vector<GMatrix*> confusionMatrices;
	double mse = pSupLearner->trainAndTest(*pFeatures1, *pLabels1, *pFeatures2, *pLabels2) / pFeatures2->rows();
	GVec::print(cout, 14, results, pLabels1->cols());
	cout << "Mean squared error: " << to_str(mse) << "\n";
/*
	// Print the confusion matrix
	if(confusion){
		printConfusionMatrices(pLabels2->relation().get(), confusionMatrices);
	}
	if(confusioncsv){
		printMachineReadableConfusionMatrices(pLabels2->relation().get(), confusionMatrices);
	}
*/
}

void GLearnerLib::SplitTest(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	double trainRatio = 0.5;
	size_t reps = 1;
	string lastModelFile="";
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed")){
			seed = args.pop_uint();
		}else if(args.if_pop("-trainratio")){
			trainRatio = args.pop_double();
		}else if(args.if_pop("-reps")){
			reps = args.pop_uint();
		}else if(args.if_pop("-writelastmodel")){
			lastModelFile = args.pop_string();
		}else{
			throw Ex("Invalid splittest option: ", args.peek());
		}
	}
	if(trainRatio < 0 || trainRatio > 1)
		throw Ex("trainratio must be between 0 and 1");

	// Load the data
	GRand prng(seed);
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();

	// Instantiate the modeler
	GTransducer* pSupLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
	Holder<GTransducer> hModel(pSupLearner);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());
	pSupLearner->rand().setSeed(seed);

	// Ensure that can write if we are required to
	if(!pSupLearner->canGeneralize() && lastModelFile != ""){
	  throw Ex("The learner specified does not have an internal model "
		     "and thus cannot be saved to a file.  Remove the "
		     "-lastModelFile argument.");
	}

	// Do the reps
	size_t trainingPatterns = std::max((size_t)1, std::min(pFeatures->rows() - 1, (size_t)floor(pFeatures->rows() * trainRatio + 0.5)));
	size_t testPatterns = pFeatures->rows() - trainingPatterns;
	// Results is the mean results for all columns (plus some extra
	// storage to make allocation and deallocation easier)
	double sumMSE = 0.0;
	for(size_t i = 0; i < reps; i++)
	{
		// Shuffle and split the data
		pFeatures->shuffle(prng, pLabels);
		GMatrix testFeatures(pFeatures->relation().clone());
		GMatrix testLabels(pLabels->relation().clone());
		{
			GMergeDataHolder hFeatures(*pFeatures, testFeatures);
			GMergeDataHolder hLabels(*pLabels, testLabels);
			testFeatures.reserve(testPatterns);
			testLabels.reserve(testPatterns);
			pFeatures->splitBySize(testFeatures, testPatterns);
			pLabels->splitBySize(testLabels, testPatterns);

			// Test and print results
			vector<GMatrix*> confusionMatrices;
			double mse = pSupLearner->trainAndTest(*pFeatures, *pLabels, testFeatures, testLabels) / testFeatures.rows();

			// Write trained model file on last repetition
			if(lastModelFile != "" && i+1 == reps){
				GSupervisedLearner* pSup =
				  dynamic_cast<GSupervisedLearner*>
				  (pSupLearner);
				GDom doc;
				GDomNode* pRoot = pSup->serialize(&doc);
				doc.setRoot(pRoot);
				std::ofstream out(lastModelFile.c_str());
				if(out){
					doc.writeJson(out);
				}
			}
			cout << "rep " << i << ") ";
			cout << "Mean squared error: " << to_str(mse) << "\n";
			sumMSE += mse;
		}
	}
	if(pLabels->cols() > 1){
	  cout << "-----Average-----\n";
	}
	cout << "Mean squared error: " << to_str(sumMSE / reps) << "\n";
}

void GLearnerLib::CrossValidateCallback(void* pSupLearner, size_t nRep, size_t nFold, double foldSSE, size_t rows)
{
	cout << "Rep: " << nRep << ", Fold: " << nFold <<", Mean squared error: " << to_str(foldSSE / rows) << "\n";
}

void GLearnerLib::CrossValidate(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	int reps = 5;
	int folds = 2;
	bool succinct = false;
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else if(args.if_pop("-reps"))
			reps = args.pop_uint();
		else if(args.if_pop("-folds"))
			folds = args.pop_uint();
		else if(args.if_pop("-succinct"))
			succinct = true;
		else
			throw Ex("Invalid crossvalidate option: ", args.peek());
	}
	if(reps < 1)
		throw Ex("There must be at least 1 rep.");
	if(folds < 2)
		throw Ex("There must be at least 2 folds.");

	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();

	// Instantiate the modeler
	GTransducer* pSupLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
	Holder<GTransducer> hModel(pSupLearner);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());
	pSupLearner->rand().setSeed(seed);

	// Test
	cout.precision(8);
	double sse = pSupLearner->repValidate(*pFeatures, *pLabels, reps, folds, succinct ? NULL : CrossValidateCallback, pSupLearner);
	if(!succinct)
		cout << "Mean squared error: ";
	cout << to_str(sse / pFeatures->rows());
	if(!succinct)
		cout << "\n";
}

void GLearnerLib::vette(string& s)
{
	for(size_t i = 0; i < s.length(); i++)
	{
		if(s[i] <= ' ' || s[i] == '\'' || s[i] == '"')
			s[i] = '_';
	}
}

void GLearnerLib::PrecisionRecall(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	int reps = 5;
	int samples = 100;
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else if(args.if_pop("-reps"))
			reps = args.pop_uint();
		else if(args.if_pop("-samples"))
			samples = args.pop_uint();
		else
			throw Ex("Invalid precisionrecall option: ", args.peek());
	}
	if(reps < 1)
		throw Ex("There must be at least 1 rep.");
	if(samples < 2)
		throw Ex("There must be at least 2 samples.");

	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();

	// Instantiate the modeler
	GTransducer* pSupLearner = InstantiateAlgorithm(args, pFeatures, pLabels);
	Holder<GTransducer> hModel(pSupLearner);
	pSupLearner->rand().setSeed(seed);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());
	if(!pSupLearner->canGeneralize())
		throw Ex("This algorithm cannot be \"trained\". It can only be used to \"transduce\".");
	GSupervisedLearner* pModel = (GSupervisedLearner*)pSupLearner;

	// Build the relation for the results
	GArffRelation* pRelation = new GArffRelation();
	pRelation->setName("untitled");
	GArffRelation* pRel = pRelation;
	pRel->addAttribute("recall", 0, NULL);
	for(size_t i = 0; i < pLabels->cols(); i++)
	{
		size_t valCount = std::max((size_t)1, pLabels->relation().valueCount(i));
		for(int val = 0; val < (int)valCount; val++)
		{
			string s = "precision_";
			if(pLabels->relation().type() == GRelation::ARFF)
				s += ((const GArffRelation&)pLabels->relation()).attrName(i);
			else
			{
				s += "attr";
				s += to_str(i);
			}
			if(valCount > 1)
			{
				s += "_";
				ostringstream oss;
				pLabels->relation().printAttrValue(oss, i, val);
				s += oss.str();
			}
			vette(s);
			pRel->addAttribute(s.c_str(), 0, NULL);
		}
	}

	// Measure precision/recall
	GMatrix results(pRelation);
	results.newRows(samples);
	for(int i = 0; i < samples; i++)
		results.row(i)[0] = (double)i / samples;
	size_t pos = 1;
	for(size_t i = 0; i < pLabels->cols(); i++)
	{
		size_t valCount = std::max((size_t)1, pLabels->relation().valueCount(i));
		double* precision = new double[valCount * samples];
		ArrayHolder<double> hPrecision(precision);
		pModel->precisionRecall(precision, samples, *pFeatures, *pLabels, i, reps);
		for(size_t j = 0; j < valCount; j++)
			results.setCol(pos++, precision + samples * j);
	}
	GAssert(pos == pRelation->size()); // counting problem
	results.print(cout);
}

void GLearnerLib::sterilize(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	size_t folds = 10;
	double diffThresh = 0.1;
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else if(args.if_pop("-folds"))
			folds = args.pop_uint();
		else if(args.if_pop("-diffthresh"))
			diffThresh = args.pop_double();
		else
			throw Ex("Invalid option: ", args.peek());
	}

	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();

	// Instantiate the modeler
	GTransducer* pTransducer = InstantiateAlgorithm(args, pFeatures, pLabels);
	Holder<GTransducer> hModel(pTransducer);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());
	pTransducer->rand().setSeed(seed);

	// Sterilize
	GMatrix sterileFeatures(pFeatures->relation().clone());
	GReleaseDataHolder hSterileFeatures(&sterileFeatures);
	GMatrix sterileLabels(pLabels->relation().clone());
	GReleaseDataHolder hSterileLabels(&sterileLabels);
	for(size_t fold = 0; fold < folds; fold++)
	{
		// Split the data
		GMatrix trainFeatures(pFeatures->relation().clone());
		GReleaseDataHolder hTrainFeatures(&trainFeatures);
		GMatrix trainLabels(pLabels->relation().clone());
		GReleaseDataHolder hTrainLabels(&trainLabels);
		GMatrix testFeatures(pFeatures->relation().clone());
		GReleaseDataHolder hTestFeatures(&testFeatures);
		GMatrix testLabels(pLabels->relation().clone());
		GReleaseDataHolder hTestLabels(&testLabels);
		size_t foldBegin = fold * pFeatures->rows() / folds;
		size_t foldEnd = (fold + 1) * pFeatures->rows() / folds;
		for(size_t i = 0; i < foldBegin; i++)
		{
			trainFeatures.takeRow(pFeatures->row(i));
			trainLabels.takeRow(pLabels->row(i));
		}
		for(size_t i = foldBegin; i < foldEnd; i++)
		{
			testFeatures.takeRow(pFeatures->row(i));
			testLabels.takeRow(pLabels->row(i));
		}
		for(size_t i = foldEnd; i < pFeatures->rows(); i++)
		{
			trainFeatures.takeRow(pFeatures->row(i));
			trainLabels.takeRow(pLabels->row(i));
		}

		// Transduce
		GMatrix* pPredictedLabels = pTransducer->transduce(trainFeatures, trainLabels, testFeatures);
		Holder<GMatrix> hPredictedLabels(pPredictedLabels);

		// Keep only the correct predictions
		for(size_t j = 0; j < testLabels.rows(); j++)
		{
			double* pTarget = testLabels[j];
			double* pPredicted = pPredictedLabels->row(j);
			for(size_t i = 0; i < testLabels.cols(); i++)
			{
				size_t vals = testLabels.relation().valueCount(i);
				bool goodEnough = false;
				if(vals == 0)
				{
					if(std::abs(*pTarget - *pPredicted) < diffThresh)
						goodEnough = true;
				}
				else
				{
					if(*pTarget == *pPredicted)
						goodEnough = true;
				}
				if(goodEnough)
				{
					sterileFeatures.takeRow(testFeatures[j]);
					sterileLabels.takeRow(testLabels[j]);
				}
				pTarget++;
				pPredicted++;
			}
		}
	}

	// Merge the sterile features and labels
	GMatrix* pSterile = GMatrix::mergeHoriz(&sterileFeatures, &sterileLabels);
	Holder<GMatrix> hSterile(pSterile);
	pSterile->print(cout);
}

void GLearnerLib::trainRecurrent(GArgReader& args)
{
	// Parse options
	unsigned int seed = getpid() * (unsigned int)time(NULL);
	vector<size_t> paramDims;
	const char* stateFilename = NULL;
	double validationInterval = 0;
	vector<string> validationFilenames;
	const char* outFilename = "model.json";
	double trainTime = 60 * 60; // 1 hour
	bool useIsomap = false;
	while(args.next_is_flag())
	{
		if(args.if_pop("-seed"))
			seed = args.pop_uint();
		else if(args.if_pop("-paramdims"))
		{
			unsigned int count = args.pop_uint();
			for(unsigned int i = 0; i < count; i++)
				paramDims.push_back(args.pop_uint());
		}
		else if(args.if_pop("-state"))
			stateFilename = args.pop_string();
		else if(args.if_pop("-validate"))
		{
			validationInterval = args.pop_double();
			int count = args.pop_uint();
			for(int i = 0; i < count; i++)
			{
				validationFilenames.push_back(args.pop_string());
				validationFilenames.push_back(args.pop_string());
			}
		}
		else if(args.if_pop("-out"))
			outFilename = args.pop_string();
		else if(args.if_pop("-traintime"))
			trainTime = args.pop_double();
		else if(args.if_pop("-isomap"))
			useIsomap = true;
		else
			throw Ex("Invalid trainRecurrent option: ", args.peek());
	}

	// Parse the algorithm
	const char* alg = args.pop_string();
//	int bpttDepth = 0;
//	int bpttItersPerGrow = 0;
	double annealDeviation = 0.0;
	double annealDecay = 0.0;
	double annealTimeWindow = 0.0;
	if(strcmp(alg, "moses") == 0)
	{
	}
/*	else if(strcmp(alg, "bptt") == 0)
	{
		bpttDepth = args.pop_uint();
		bpttItersPerGrow = args.pop_uint();
	}
	else if(strcmp(alg, "bpttcal") == 0)
	{
		bpttDepth = args.pop_uint();
		bpttItersPerGrow = args.pop_uint();
	}*/
	else if(strcmp(alg, "evolutionary") == 0)
	{
	}
	else if(strcmp(alg, "hillclimber") == 0)
	{
	}
	else if(strcmp(alg, "annealing") == 0)
	{
		annealDeviation = args.pop_double();
		annealDecay = args.pop_double();
		annealTimeWindow = args.pop_double();
	}
	else
		throw Ex("Unrecognized recurrent model training algorithm: ", alg);

	// Load the data
	GMatrix dataObs;
	dataObs.loadArff(args.pop_string());
	GMatrix dataAction;
	dataAction.loadArff(args.pop_string());

	// Get the number of context dims
	int contextDims = args.pop_uint();

	// Infer remaining values and check that the parts fit together
	size_t pixels = 1;
	for(vector<size_t>::iterator it = paramDims.begin(); it != paramDims.end(); it++)
		pixels *= *it;
	size_t channels = dataObs.cols() / pixels;
	if((channels * pixels) != dataObs.cols())
		throw Ex("The number of columns in the observation data must be a multiple of the product of the param dims");

	// Instantiate the recurrent model
	GTransducer* pTransitionFunc = InstantiateAlgorithm(args, NULL, NULL);
	Holder<GTransducer> hTransitionFunc(pTransitionFunc);
	if(!pTransitionFunc->canGeneralize())
		throw Ex("The algorithm specified for the transition function cannot be \"trained\". It can only be used to \"transduce\".");
	pTransitionFunc->rand().setSeed(seed);
	GTransducer* pObservationFunc = InstantiateAlgorithm(args, NULL, NULL);
	Holder<GTransducer> hObservationFunc(pObservationFunc);
	if(!pObservationFunc->canGeneralize())
		throw Ex("The algorithm specified for the observation function cannot be \"trained\". It can only be used to \"transduce\".");
	pObservationFunc->rand().setSeed((seed + 13) * 11);
	if(args.size() > 0)
		throw Ex("Superfluous argument: ", args.peek());
	GRand prng(seed);
	MyRecurrentModel model((GSupervisedLearner*)hTransitionFunc.release(), (GSupervisedLearner*)hObservationFunc.release(), dataAction.cols(), contextDims, dataObs.cols(), &prng, &paramDims, stateFilename, validationInterval);

	// Set it up to do validation during training if specified
	vector<GMatrix*> validationData;
	VectorOfPointersHolder<GMatrix> hValidationData(validationData);
	if(validationInterval > 0)
	{
		for(size_t i = 0; i < validationFilenames.size(); i++)
		{
			GMatrix* pVal = new GMatrix();
			Holder<GMatrix> hVal(pVal);
			pVal->loadArff(validationFilenames[i].c_str());
			validationData.push_back(hVal.release());
		}
		model.validateDuringTraining(validationInterval, &validationData);
		cout << "@RELATION validation_scores\n\n@ATTRIBUTE seconds real\n@ATTRIBUTE " << alg << " real\n\n@DATA\n";
	}

	// Set other flags
	model.setTrainingSeconds(trainTime);
	model.setUseIsomap(useIsomap);

	// Do the training
	if(strcmp(alg, "moses") == 0)
		model.trainMoses(&dataAction, &dataObs);
/*	else if(strcmp(alg, "bptt") == 0)
		model.trainBackPropThroughTime(&dataAction, &dataObs, bpttDepth, bpttItersPerGrow);*/
	else if(strcmp(alg, "evolutionary") == 0)
		model.trainEvolutionary(&dataAction, &dataObs);
	else if(strcmp(alg, "hillclimber") == 0)
		model.trainHillClimber(&dataAction, &dataObs, 0.0, 0.0, 0.0, true, false);
	else if(strcmp(alg, "annealing") == 0)
		model.trainHillClimber(&dataAction, &dataObs, annealDeviation, annealDecay, annealTimeWindow, false, true);
	GDom doc;
	doc.setRoot(model.serialize(&doc));
	doc.saveJson(outFilename);
}

void GLearnerLib::regress(GArgReader& args)
{
	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();
	if(pLabels->cols() != 1)
		throw Ex("Sorry, only 1 label dimension currently supported");

	// Load the equation
	string expr;
	while(args.size() > 0)
		expr += args.pop_string();
	GFunctionParser fp(expr.c_str());
	GFunction* pFunc = fp.getFunctionNoThrow("f");
	if(!pFunc)
		throw Ex("Expected a function named \"f\".");
	if((size_t)pFunc->m_expectedParams <= pFeatures->cols())
		throw Ex("Expected more than", to_str(pFeatures->cols()), " params. Got only ", to_str(pFunc->m_expectedParams));

	// Optimize
	OptimizerTargetFunc tf(pFeatures, pLabels, pFunc);
	GHillClimber hc(&tf);
	hc.searchUntil(10000, 200, 0.01);
	double err = hc.currentError();
	cout << "SSE = " << to_str(err) << "\n";
	cout << "Params:\n";
	double* pVec = hc.currentVector();
	for(size_t i = 0; i < (size_t)pFunc->m_expectedParams - pFeatures->cols(); i++)
	{
		if(i > 0)
			cout << ", ";
		cout << to_str(pVec[i]);
	}
	cout << "\n";
}

void GLearnerLib::metaData(GArgReader& args)
{
	// Load the data
	Holder<GMatrix> hFeatures, hLabels;
	loadData(args, hFeatures, hLabels);
	GMatrix* pFeatures = hFeatures.get();
	GMatrix* pLabels = hLabels.get();

	// Make the relation
	GArffRelation* pRel = new GArffRelation();
	pRel->addAttribute("log_rows", 0, NULL);
	pRel->addAttribute("log_feature_dims", 0, NULL);
	pRel->addAttribute("log_label_dims", 0, NULL);
	pRel->addAttribute("log_feature_elements", 0, NULL);
	pRel->addAttribute("log_sum_feature_vals", 0, NULL);
	pRel->addAttribute("mean_feature_vals", 0, NULL);
	pRel->addAttribute("feature_range_deviation", 0, NULL);
	pRel->addAttribute("feature_portion_real", 0, NULL);
	pRel->addAttribute("label_portion_real", 0, NULL);
	pRel->addAttribute("feature_is_missing_values", 0, NULL);
	pRel->addAttribute("label_entropy", 0, NULL);
	pRel->addAttribute("label_skew", 0, NULL);
	pRel->addAttribute("landmark_baseline", 0, NULL);
	pRel->addAttribute("landmark_linear", 0, NULL);
	pRel->addAttribute("landmark_decisiontree", 0, NULL);
	pRel->addAttribute("landmark_shallowtree", 0, NULL);
	pRel->addAttribute("landmark_meanmarginstree", 0, NULL);
	pRel->addAttribute("landmark_naivebayes", 0, NULL);

	// Make the meta-data
	GMatrix meta(pRel);
	double* pRow = meta.newRow();

	// log_rows
	*(pRow++) = log((double)pFeatures->rows());

	// log_feature_dims
	*(pRow++) = log((double)pFeatures->cols());

	// log_label_dims
	*(pRow++) = log((double)pLabels->cols());

	// log_feature_elements
	*(pRow++) = log((double)(pFeatures->rows() * pFeatures->cols()));

	// log_sum_feature_vals
	size_t sum = 0;
	for(size_t i = 0; i < pFeatures->cols(); i++)
		sum += pFeatures->relation().valueCount(i);
	*(pRow++) = log((double)(sum + 1));

	// mean_feature_vals
	*(pRow++) = (double)sum / pFeatures->cols();

	// feature_range_deviation
	{
		double s = 0.0;
		double ss = 0.0;
		for(size_t i = 0; i < pFeatures->cols(); i++)
		{
			double range = pFeatures->columnMax(i) - pFeatures->columnMin(i);
			s += range;
			ss += (range * range);
		}
		s /= pFeatures->cols();
		ss /= pFeatures->cols();
		*(pRow++) = (double)(pFeatures->cols() - 1) / pFeatures->cols() * sqrt(ss - (s * s));
	}

	// feature_portion_real
	size_t realCount = 0;
	for(size_t i = 0; i < pFeatures->cols(); i++)
	{
		if(pFeatures->relation().valueCount(i) == 0)
			realCount++;
	}
	*(pRow++) = (double)realCount / pFeatures->cols();

	// label_portion_real
	realCount = 0;
	for(size_t i = 0; i < pLabels->cols(); i++)
	{
		if(pLabels->relation().valueCount(i) == 0)
			realCount++;
	}
	*(pRow++) = (double)realCount / pLabels->cols();

	// features_is_missing_values
	*(pRow++) = pFeatures->doesHaveAnyMissingValues() ? 1.0 : 0.0;

	// label_entropy
	double dsum = 0.0;
	for(size_t i = 0; i < pLabels->cols(); i++)
	{
		if(pLabels->relation().valueCount(i) == 0)
		{
			double mean = pLabels->columnMean(i);
			dsum += log(1.0 + sqrt(pLabels->columnVariance(i, mean)));
		}
		else
			dsum += pLabels->entropy(i);
	}
	*(pRow++) = dsum / pLabels->cols();

	// label_skew
	dsum = 0.0;
	for(size_t i = 0; i < pLabels->cols(); i++)
	{
		if(pLabels->relation().valueCount(i) == 0)
		{
			double mean = pLabels->columnMean(i);
			double median = pLabels->columnMedian(i);
			dsum += log(1.0 + std::abs(mean - median));
		}
		else
		{
			double mostCommonValue = pLabels->baselineValue(i);
			size_t count = 0;
			for(size_t j = 0; j < pLabels->rows(); j++)
			{
				if(pLabels->row(j)[i] == mostCommonValue)
					count++;
			}
			dsum += (double)count / pLabels->rows();
		}
	}
	*(pRow++) = dsum / pLabels->cols();

	// landmark_baseline
	{
		GBaselineLearner model;
		*(pRow++) = model.repValidate(*pFeatures, *pLabels, 5, 2) / pFeatures->rows();
	}

	// landmark_linear
	{
		GLinearRegressor model;
		*(pRow++) = model.repValidate(*pFeatures, *pLabels, 5, 2) / pFeatures->rows();
	}

	// landmark_decisiontree
	{
		GDecisionTree model;
		model.useBinaryDivisions();
		*(pRow++) = model.repValidate(*pFeatures, *pLabels, 5, 2) / pFeatures->rows();
	}

	// landmark_shallowtree
	{
		GDecisionTree model;
		model.useBinaryDivisions();
		model.setLeafThresh(24);
		*(pRow++) = model.repValidate(*pFeatures, *pLabels, 5, 2) / pFeatures->rows();
	}

	// landmark_meanmarginstree
	{
		GMeanMarginsTree model;
		*(pRow++) = model.repValidate(*pFeatures, *pLabels, 5, 2) / pFeatures->rows();
	}

	// landmark_naivebayes
	{
		GNaiveBayes model;
		*(pRow++) = model.repValidate(*pFeatures, *pLabels, 5, 2) / pFeatures->rows();
	}

	// Print the results
	meta.print(cout);
}

void GLearnerLib::ShowUsage(const char* appName)
{
	cout << "Full Usage Information\n";
	cout << "[Square brackets] are used to indicate required arguments.\n";
	cout << "<Angled brackets> are used to indicate optional arguments.\n";
	cout << "\n";
	UsageNode* pUsageTree = makeLearnUsageTree();
	Holder<UsageNode> hUsageTree(pUsageTree);
	pUsageTree->print(cout, 0, 3, 76, 1000, true);
	UsageNode* pUsageTree2 = makeAlgorithmUsageTree();
	Holder<UsageNode> hUsageTree2(pUsageTree2);
	pUsageTree2->print(cout, 0, 3, 76, 1000, true);
	cout.flush();
}

void GLearnerLib::showError(GArgReader& args, const char* szAppName, const char* szMessage)
{
	cerr << "_________________________________\n";
	cerr << szMessage << "\n\n";
	args.set_pos(1);
	const char* szCommand = args.peek();
	UsageNode* pUsageTree = makeLearnUsageTree();
	Holder<UsageNode> hUsageTree(pUsageTree);
	if(szCommand)
	{
		UsageNode* pUsageCommand = pUsageTree->choice(szCommand);
		if(pUsageCommand)
		{
			cerr << "Brief Usage Information:\n\n";
			cerr << szAppName << " ";
			pUsageCommand->print(cerr, 0, 3, 76, 1000, true);
			if(pUsageCommand->findPart("[algorithm]") >= 0)
			{
				UsageNode* pAlgTree = makeAlgorithmUsageTree();
				Holder<UsageNode> hAlgTree(pAlgTree);
				pAlgTree->print(cerr, 1, 3, 76, 2, false);
			}
		}
		else
		{
			cerr << "Brief Usage Information:\n\n";
			pUsageTree->print(cerr, 0, 3, 76, 1, false);
		}
	}
	else
	{
		pUsageTree->print(cerr, 0, 3, 76, 1, false);
		cerr << "\nFor more specific usage information, enter as much of the command as you know.\n";
	}
	cerr << "\nTo see full usage information, run:\n	" << szAppName << " usage\n\n";
	cerr << "For a graphical tool that will help you to build a command, run:\n	waffles_wizard\n";
	cerr.flush();
}