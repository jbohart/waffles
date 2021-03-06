/*
  The contents of this file are dedicated by all of its authors, including

    Michael S. Gashler,
    Michael R. Smith,
    Stephen Ashmore,
    anonymous contributors,

  to the public domain (http://creativecommons.org/publicdomain/zero/1.0/).

  Note that some moral obligations still exist in the absence of legal ones.
  For example, it would still be dishonest to deliberately misrepresent the
  origin of a work. Although we impose no legal requirements to obtain a
  license, it is beseeming for those who build on the works of others to
  give back useful improvements, or find a way to pay it forward. If
  you would like to cite us, a published paper about Waffles can be found
  at http://jmlr.org/papers/volume12/gashler11a/gashler11a.pdf. If you find
  our code to be useful, the Waffles team would love to hear how you use it.
*/

#include "GLayer.h"
#ifndef MIN_PREDICT
#include "GMath.h"
#endif // MIN_PREDICT
#include "GActivation.h"
#ifndef MIN_PREDICT
#include "GDistribution.h"
#endif // MIN_PREDICT
#include "GError.h"
#include "GRand.h"
#include "GVec.h"
#include "GDom.h"
#ifndef MIN_PREDICT
#include "GHillClimber.h"
#endif  // MIN_PREDICT
#include "GTransform.h"
#ifndef MIN_PREDICT
#include "GSparseMatrix.h"
#include "GDistance.h"
#include "GAssignment.h"
#endif // MIN_PREDICT
#include "GHolders.h"
#include "GBits.h"
#include "GFourier.h"
#include <memory>

using std::vector;
using std::ostream;

namespace GClasses {

GDomNode* GNeuralNetLayer::baseDomNode(GDom* pDoc)
{
	GDomNode* pNode = pDoc->newObj();
	pNode->addField(pDoc, "type", pDoc->newInt(type()));
	return pNode;
}

GNeuralNetLayer* GNeuralNetLayer::deserialize(GDomNode* pNode)
{
	LayerType e = (LayerType)pNode->field("type")->asInt();
	switch(e)
	{
		case layer_tanh: return new GLayerTanh(pNode);
		case layer_logistic: return new GLayerLogistic(pNode);
		case layer_bentidentity: return new GLayerBentIdentity(pNode);
		case layer_softroot: return new GLayerSoftRoot(pNode);
		case layer_sigexp: return new GLayerSigExp(pNode);
		case layer_gaussian: return new GLayerGaussian(pNode);
		case layer_sine: return new GLayerSine(pNode);
		case layer_rectifier: return new GLayerRectifier(pNode);
		case layer_leakyrectifier: return new GLayerLeakyRectifier(pNode);
		case layer_softplus: return new GLayerSoftPlus(pNode);
		case layer_linear: return new GLayerLinear(pNode);
//		case layer_activation: return new GLayerActivation(pNode);
		case layer_restrictedboltzmannmachine: return new GLayerRestrictedBoltzmannMachine(pNode);
		case layer_convolutional1d: return new GLayerConvolutional1D(pNode);
		case layer_convolutional2d: return new GLayerConvolutional2D(pNode);
		default: throw Ex("Unrecognized neural network layer type: ", GClasses::to_str((int)e));
	}
}

GMatrix* GNeuralNetLayer::feedThrough(const GMatrix& data)
{
	size_t outputCount = outputs();
	GMatrix* pResults = new GMatrix(0, outputCount);
	for(size_t i = 0; i < data.rows(); i++)
	{
		feedForward(data[i]);
		pResults->newRow().copy(activation());
	}
	return pResults;
}




GLayerLinear::GLayerLinear(size_t out)
{
	resize(FLEXIBLE_SIZE, out);
}

GLayerLinear::GLayerLinear(size_t in, size_t out)
{
	resize(in, out);
}

GLayerLinear::GLayerLinear(GDomNode* pNode) : m_weights(pNode->field("weights")), m_activation(2, m_weights.cols())
{}

GDomNode* GLayerLinear::serialize(GDom* pDoc)
{
	GDomNode* pNode = baseDomNode(pDoc);
	pNode->addField(pDoc, "weights", m_weights.serialize(pDoc));
	return pNode;
}

/// Makes a string representation of this layer
std::string GLayerLinear::to_str()
{
	std::ostringstream oss;
	oss << "[GLayerLinear:" << inputs() << "->" << outputs() << "]";
	return oss.str();
}

void GLayerLinear::resize(size_t in, size_t out)
{
	if(in == inputs() && out == outputs())
		return;

	m_weights.resize(in + 1, out);
	m_activation.resize(2, out);
}

void GLayerLinear::feedForward(const GVec& in)
{
	GAssert(bias()[outputs() - 1] > -1e100 && bias()[outputs() - 1] < 1e100);

	GVec &a = activation();
	a.copy(bias());

	for(size_t i = 0; i < inputs(); i++)
		a.addScaled(in[i], m_weights.row(i));
}

void GLayerLinear::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GVec &upStreamError = pUpStreamLayer->error();
	size_t inputCount = pUpStreamLayer->outputs();
	GAssert(inputCount <= inputs());
	const GVec &source = error();
	for(size_t i = 0; i < inputCount; i++)
		upStreamError[i] = source.dotProduct(m_weights[i]);
}

void GLayerLinear::updateDeltas(const GVec& upStreamActivation, GVec &deltas)
{
	GAssert(deltas.size() == countWeights(), "Deltas must match the dimensions of weights!");
	GVec &err = error();
	double *delta = deltas.data();
	for(size_t i = 0; i < inputs(); ++i)
	{
		double act = upStreamActivation[i];
		for(size_t j = 0; j < outputs(); ++j)
			*delta++ += err[j] * act;
	}
	for(size_t j = 0; j < outputs(); ++j)
		*delta++ += err[j];
}

void GLayerLinear::applyDeltas(double learningRate, const GVec &deltas)
{
	GAssert(deltas.size() == countWeights(), "Deltas must match the dimensions of weights!");
	const double *delta = deltas.data();
	GVec &b = bias();
	for(size_t i = 0; i < inputs(); ++i)
		for(size_t j = 0; j < outputs(); ++j)
			m_weights[i][j] += learningRate * *delta++;
	for(size_t j = 0; j < outputs(); ++j)
		b[j] += learningRate * *delta++;
}

void GLayerLinear::scaleWeights(double factor, bool scaleBiases)
{
	for(size_t i = 0; i < inputs(); i++)
		m_weights[i] *= factor;
	if(scaleBiases)
		bias() *= factor;
}

void GLayerLinear::diminishWeights(double amount, bool regularizeBiases)
{
	for(size_t i = 0; i < inputs(); i++)
		m_weights[i].regularizeL1(amount);
	if(regularizeBiases)
		bias().regularizeL1(amount);
}

void GLayerLinear::contractWeights(double factor, bool contractBiases)
{
	GVec& a = activation();
	GVec& b = bias();
	size_t outputCount = outputs();
	for(size_t i = 0; i < outputCount; i++)
	{
		// (Note that the official implementation of contractive regularization multiplies by the
		// derivative of the activation function, but we separate activation functions into separate
		// layers, and I don't think they should depend on each other, so this implementation
		// assumes the tanh activation function for regularization purposes.)
		double activ = tanh(a[i]);
		double aprime = 1.0 - (activ * activ);
		double f = 1.0 - factor * aprime;
		for(size_t j = 0; j < inputs(); j++)
			m_weights[j][i] *= f;
		if(contractBiases)
			b[i] *= f;
	}
}

size_t GLayerLinear::countWeights()
{
	return (inputs() + 1) * outputs();
}

size_t GLayerLinear::weightsToVector(double *pOutVector)
{
	m_weights.toVector(pOutVector);
	return countWeights();
}

size_t GLayerLinear::vectorToWeights(const double *pVector)
{
	m_weights.fromVector(pVector, inputs() + 1);
	return countWeights();
}

void GLayerLinear::copyWeights(const GNeuralNetLayer *pSource)
{
	GLayerLinear *src = (GLayerLinear *) pSource;
	m_weights.copyBlock(src->m_weights, 0, 0, INVALID_INDEX, INVALID_INDEX, 0, 0, false);
}

void GLayerLinear::resetWeights(GRand& rand)
{
	size_t outputCount = outputs();
	size_t inputCount = inputs();
	double mag = std::max(0.03, 1.0 / inputCount);
	for(size_t i = 0; i < m_weights.rows(); i++)
	{
		GVec& w = m_weights[i];
		for(size_t j = 0; j < outputCount; j++)
			w[j] = rand.normal() * mag;
	}
}

void GLayerLinear::perturbWeights(GRand &rand, double deviation, size_t start, size_t count)
{
	size_t n = std::min(outputs() - start, count);
	for(size_t j = 0; j < m_weights.rows(); j++)
		GVec::perturb(m_weights[j].data() + start, deviation, n, rand);
}

void GLayerLinear::maxNorm(double min, double max)
{
	size_t outputCount = outputs();
	size_t inputCount = inputs();
	for(size_t i = 0; i < outputCount; i++)
	{
		double squaredMag = 0;
		for(size_t j = 0; j < inputCount; j++)
		{
			double d = m_weights[j][i];
			squaredMag += (d * d);
		}
		if(squaredMag > max * max)
		{
			double scal = max / sqrt(squaredMag);
			for(size_t j = 0; j < inputCount; j++)
				m_weights[j][i] *= scal;
		}
		else if(squaredMag < min * min)
		{
			if(squaredMag == 0.0)
			{
				for(size_t j = 0; j < inputCount; j++)
					m_weights[j][i] = 1.0;
				squaredMag = (double) inputCount;
			}
			double scal = min / sqrt(squaredMag);
			for(size_t j = 0; j < inputCount; j++)
				m_weights[j][i] *= scal;
		}
	}
}

void GLayerLinear::renormalizeInput(size_t input, double oldMin, double oldMax, double newMin, double newMax)
{
	size_t outputCount = outputs();
	GVec& w = m_weights[input];
	GVec& b = bias();
	double f = (oldMax - oldMin) / (newMax - newMin);
	double g = (oldMin - newMin * f);
	for(size_t i = 0; i < outputCount; i++)
	{
		b[i] += (w[i] * g);
		w[i] *= f;
	}
}

void GLayerLinear::transformWeights(GMatrix& transform, const GVec& offset)
{
	if(transform.rows() != inputs())
		throw Ex("Transformation matrix not suitable size for this layer");
	if(transform.rows() != transform.cols())
		throw Ex("Expected a square transformation matrix.");
	size_t outputCount = outputs();

	GMatrix temp(inputs(), outputs());
	temp.copyBlock(m_weights, 0, 0, inputs(), outputs());

	GMatrix* pNewWeights = GMatrix::multiply(transform, temp, true, false);
	std::unique_ptr<GMatrix> hNewWeights(pNewWeights);
	m_weights.copyBlock(*pNewWeights, 0, 0, pNewWeights->rows(), outputCount, 0, 0, false);
	GVec n(outputs());
	n.fill(0.0);
	for(size_t i = 0; i < inputs(); i++)
		n.addScaled(offset[i], m_weights.row(i));
	bias() += n;
}






GLayerActivation::GLayerActivation()
: m_activation(2, 0)
{}

GLayerActivation::GLayerActivation(GDomNode* pNode)
: m_activation(2, pNode->field("size")->asInt())
{}

GDomNode* GLayerActivation::serialize(GDom* pDoc)
{
	GDomNode* pNode = baseDomNode(pDoc);
	pNode->addField(pDoc, "size", pDoc->newInt(m_activation.cols()));
	return pNode;
}

/// Makes a string representation of this layer
std::string GLayerActivation::to_str()
{
	std::ostringstream oss;
	oss << "[GLayerActivation: type=" << (size_t)type() << ", size=" << inputs() << "]";
	return oss.str();
}

void GLayerActivation::resize(size_t in, size_t out)
{
	if(in != out)
		throw Ex("GLayerActivation must have the same number of inputs as outputs.");
	m_activation.resize(2, out);
}

void GLayerActivation::feedForward(const GVec& in)
{
	GVec &a = activation();
	for(size_t i = 0; i < inputs(); i++)
		a[i] = eval(in[i]);
}

void GLayerActivation::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GVec &upStreamError = pUpStreamLayer->error();
	size_t inputCount = pUpStreamLayer->outputs();
	GAssert(inputCount <= inputs());
	const GVec &source = error();
	GVec &n = pUpStreamLayer->activation();
	GVec &a = activation();
	for(size_t i = 0; i < inputCount; i++)
		upStreamError[i] = source[i] * derivative(n[i], a[i]);
}

size_t GLayerActivation::countWeights()
{
	return 0;
}










GLayerProductPooling::GLayerProductPooling(size_t inps)
{
	if((inps & 1) == 1)
		throw Ex("inputCount must be divisible by 2");
	resize(inps, inps / 2);
}

GLayerProductPooling::GLayerProductPooling(GDomNode* pNode)
{
	throw Ex("Sorry, not implemented yet");
}

GLayerProductPooling::~GLayerProductPooling()
{
}

GDomNode* GLayerProductPooling::serialize(GDom* pDoc)
{
	throw Ex("Sorry, not implemented yet");
}

// virtual
std::string GLayerProductPooling::to_str()
{
	std::ostringstream os;
	os << "[GLayerProductPooling:" << GClasses::to_str(inputs()) << "->" << GClasses::to_str(outputs()) << "]\n";
	return os.str();
}

void GLayerProductPooling::resize(size_t inputCount, size_t outputCount)
{
	if(outputCount * 2 != inputCount)
		throw Ex("inputCount must be 2*outputCount");
	if(outputCount == outputs())
		return;

	// Weights
	m_activation.resize(2, outputCount);
}

// virtual
void GLayerProductPooling::feedForward(const GVec& in)
{
	if(in.size() != m_activation.cols() * 2)
		throw Ex("Unexpected number of inputs");
	GVec& a = activation();
	for(size_t i = 0; i < m_activation.cols(); i++)
		a[i] = in[2 * i] * in[2 * i + 1];
}

void GLayerProductPooling::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GVec& upStreamAct = pUpStreamLayer->activation();
	GVec& upStreamError = pUpStreamLayer->error();
	size_t outputCount = outputs();
	const GVec& err = error();
	for(size_t i = 0; i < outputCount; i++)
	{
		upStreamError[2 * i] = err[i] * upStreamAct[2 * i + 1];
		upStreamError[2 * i + 1] = err[i] * upStreamAct[2 * i];
	}
}

// virtual
size_t GLayerProductPooling::countWeights()
{
	return 0;
}







GLayerAdditionPooling::GLayerAdditionPooling(size_t inps)
{
	if((inps & 1) == 1)
		throw Ex("inputCount must be divisible by 2");
	resize(inps, inps / 2);
}

GLayerAdditionPooling::GLayerAdditionPooling(GDomNode* pNode)
{
	throw Ex("Sorry, not implemented yet");
}

GLayerAdditionPooling::~GLayerAdditionPooling()
{
}

GDomNode* GLayerAdditionPooling::serialize(GDom* pDoc)
{
	throw Ex("Sorry, not implemented yet");
}

// virtual
std::string GLayerAdditionPooling::to_str()
{
	std::ostringstream os;
	os << "[GLayerAdditionPooling:" << GClasses::to_str(inputs()) << "->" << GClasses::to_str(outputs()) << "]\n";
	return os.str();
}

void GLayerAdditionPooling::resize(size_t inputCount, size_t outputCount)
{
	if(outputCount * 2 != inputCount)
		throw Ex("inputCount must be 2*outputCount");
	if(outputCount == outputs())
		return;

	// Weights
	m_activation.resize(2, outputCount);
}

void GLayerAdditionPooling::feedForward(const GVec& in)
{
	if(in.size() != m_activation.cols() * 2)
		throw Ex("Unexpected number of inputs");
	GVec& a = activation();
	for(size_t i = 0; i < m_activation.cols(); i++)
		a[i] = in[2 * i] + in[2 * i + 1];
}

void GLayerAdditionPooling::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GVec& upStreamAct = pUpStreamLayer->activation();
	GVec& upStreamError = pUpStreamLayer->error();
	size_t outputCount = outputs();
	const GVec& err = error();
	for(size_t i = 0; i < outputCount; i++)
	{
		// Should we add here?
		upStreamError[2 * i] = err[i] + upStreamAct[2 * i + 1];
		upStreamError[2 * i + 1] = err[i] + upStreamAct[2 * i];
	}
}

// virtual
size_t GLayerAdditionPooling::countWeights()
{
	return 0;
}







GLayerMaxOut::GLayerMaxOut(size_t inps, size_t outs)
{
	resize(inps, outs);
}

GLayerMaxOut::GLayerMaxOut(GDomNode* pNode)
{
	throw Ex("Sorry, not implemented yet");
}

GLayerMaxOut::~GLayerMaxOut()
{
}

GDomNode* GLayerMaxOut::serialize(GDom* pDoc)
{
	throw Ex("Sorry, not implemented yet");
}

// virtual
std::string GLayerMaxOut::to_str()
{
	std::ostringstream os;
	os << "[GLayerMaxOut:" << GClasses::to_str(inputs()) << "->" << GClasses::to_str(outputs()) << "\n";
	os << " Weights: " << GClasses::to_str(m_weights) << "\n";
	os << " Bias: " << GClasses::to_str(m_bias) << "\n";
	os << "]";
	return os.str();
}

void GLayerMaxOut::resize(size_t inputCount, size_t outputCount)
{
	if(inputCount == inputs() && outputCount == outputs())
		return;

	// Weights
	m_weights.resize(inputCount, outputCount);
	m_activation.resize(2, outputCount);
	m_winners.resize(outputCount);

	// Bias
	m_bias.resize(2, inputCount);
}

// virtual
void GLayerMaxOut::resetWeights(GRand& rand)
{
	size_t outputCount = outputs();
	size_t inputCount = inputs();
	double mag = std::max(0.03, 1.0 / inputCount); // maxing with 0.03 helps to prevent the gradient from vanishing beyond the precision of doubles in deep networks
	for(size_t i = 0; i < inputCount; i++)
	{
		GVec& w = m_weights[i];
		for(size_t j = 0; j < outputCount; j++)
			w[j] = rand.normal() * mag;
	}
	GVec& b = bias();
	for(size_t i = 0; i < inputCount; i++)
		b[i] = rand.normal() * mag;
}

// virtual
void GLayerMaxOut::feedForward(const GVec& in)
{
	// Feed the input through
	size_t inputCount = inputs();
	size_t outputCount = outputs();
	GVec& b = bias();
	GVec& a = activation();
	for(size_t i = 0; i < outputCount; i++)
	{
		double best = -1e200;
		for(size_t j = 0; j < inputCount; j++)
		{
			double cand = (in[j] + b[j]) * m_weights[j][i];
			if(cand > best)
			{
				best = cand;
				m_winners[i] = j;
			}
		}
		if(rand() % 10 == 0) // todo: get rid of rand()
		{
			size_t j = rand() % inputCount; // todo: get rid of rand()
			best = (in[j] + b[j]) * m_weights[j][i];
			m_winners[i] = j;
		}
		a[i] = best;
	}
}

// virtual
void GLayerMaxOut::dropOut(GRand& rand, double probOfDrop)
{
	throw Ex("Sorry, not implemented");
}

void GLayerMaxOut::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GVec& upStreamError = pUpStreamLayer->error();
	GAssert(pUpStreamLayer->outputs() <= m_weights.rows());
	size_t outputCount = outputs();
	const GVec& source = error();
	upStreamError.fill(0.0);
	for(size_t i = 0; i < outputCount; i++)
	{
		size_t up = m_winners[i];
		GAssert(up < pUpStreamLayer->outputs());
		upStreamError[up] += m_weights[up][i] * source[i];
	}
}

void GLayerMaxOut::updateDeltas(const GVec &upStreamActivation, GVec &deltas)
{
	GVec& err = error();
	size_t outputCount = outputs();
	double *delta = deltas.data();
	for(size_t down = 0; down < outputCount; down++)
	{
		size_t up = m_winners[down];
		*delta++ += err[down]; // bias
		*delta++ += err[down] * upStreamActivation[up]; // weights
	}
}

void GLayerMaxOut::applyDeltas(double learningRate, const GVec &deltas)
{
	size_t outputCount = outputs();
	GVec& bi = bias();
	const double *delta = deltas.data();
	for(size_t down = 0; down < outputCount; down++)
	{
		size_t up = m_winners[down];
		bi[up] += learningRate * *delta++;
		m_weights[up][down] += learningRate * *delta++;
	}
}

void GLayerMaxOut::scaleWeights(double factor, bool scaleBiases)
{
	for(size_t i = 0; i < m_weights.rows(); i++)
		m_weights[i] *= factor;
	if(scaleBiases)
		bias() *= factor;
}

void GLayerMaxOut::diminishWeights(double amount, bool regularizeBiases)
{
	for(size_t i = 0; i < m_weights.rows(); i++)
		m_weights[i].regularizeL1(amount);
	if(regularizeBiases)
		bias().regularizeL1(amount);
}

void GLayerMaxOut::transformWeights(GMatrix& transform, const GVec& offset)
{
	throw Ex("Not implemented");
}

void GLayerMaxOut::setWeightsToIdentity(size_t start, size_t count)
{
	size_t end = std::min(start + count, outputs());
	for(size_t i = start; i < end; i++)
	{
		bias()[i] = 0.0;
		for(size_t j = 0; j < inputs(); j++)
		{
			if(j == i)
				m_weights[j][i] = 1.0;
			else
				m_weights[j][i] = 0.0;
		}
	}
}

// virtual
void GLayerMaxOut::maxNorm(double min, double max)
{
	throw Ex("Not implemented");
}

// virtual
size_t GLayerMaxOut::countWeights()
{
	return inputs() * (outputs() + 1);
}

// virtual
size_t GLayerMaxOut::weightsToVector(double* pOutVector)
{
	memcpy(pOutVector, bias().data(), sizeof(double) * inputs());
	pOutVector += inputs();
	m_weights.toVector(pOutVector);
	pOutVector += (inputs() * outputs());
	return inputs() * (outputs() + 1);
}

// virtual
size_t GLayerMaxOut::vectorToWeights(const double* pVector)
{
	bias().set(pVector, inputs());
	pVector += inputs();
	m_weights.fromVector(pVector, inputs());
	pVector += (inputs() * outputs());
	return inputs() * (outputs() + 1);
}

// virtual
void GLayerMaxOut::copyWeights(const GNeuralNetLayer* pSource)
{
	GLayerMaxOut* src = (GLayerMaxOut*)pSource;
	m_weights.copyBlock(src->m_weights, 0, 0, INVALID_INDEX, INVALID_INDEX, 0, 0, false);
	bias().copy(src->bias());
}

// virtual
void GLayerMaxOut::perturbWeights(GRand& rand, double deviation, size_t start, size_t count)
{
	size_t n = std::min(outputs() - start, count);
	for(size_t j = 0; j < m_weights.rows(); j++)
		GVec::perturb(m_weights[j].data() + start, deviation, n, rand);
	GVec::perturb(bias().data(), deviation, inputs(), rand);
}










GLayerRestrictedBoltzmannMachine::GLayerRestrictedBoltzmannMachine(size_t inputSize, size_t outputSize)
{
	resize(inputSize, outputSize);
}

GLayerRestrictedBoltzmannMachine::GLayerRestrictedBoltzmannMachine(GDomNode* pNode)
: m_weights(pNode->field("weights")), m_bias(3, m_weights.rows()), m_biasReverse(3, m_weights.cols())
{
	bias().deserialize(pNode->field("bias"));
	biasReverse().deserialize(pNode->field("biasRev"));
}

GDomNode* GLayerRestrictedBoltzmannMachine::serialize(GDom* pDoc)
{
	GDomNode* pNode = baseDomNode(pDoc);
	pNode->addField(pDoc, "weights", m_weights.serialize(pDoc));
	pNode->addField(pDoc, "bias", bias().serialize(pDoc));
	pNode->addField(pDoc, "biasRev", biasReverse().serialize(pDoc));

	return pNode;
}

// virtual
std::string GLayerRestrictedBoltzmannMachine::to_str()
{
	std::ostringstream os;
	os << "[GLayerRestrictedBoltzmannMachine:" << GClasses::to_str(inputs()) << "->" << GClasses::to_str(outputs()) << "\n";
	os << " Weights: " << GClasses::to_str(m_weights) << "\n";
	os << " Bias: " << GClasses::to_str(bias()) << "\n";
	os << " BiasReverse: " << GClasses::to_str(biasReverse()) << "\n";
	os << "]";
	return os.str();
}

void GLayerRestrictedBoltzmannMachine::resize(size_t inputCount, size_t outputCount)
{
	if(inputCount == inputs() && outputCount == outputs())
		return;

	// Weights
	m_weights.resize(outputCount, inputCount);

	// Bias
	m_bias.resize(3, outputCount);

	// BiasReverse
	m_biasReverse.resize(3, inputCount);
}

// virtual
void GLayerRestrictedBoltzmannMachine::resetWeights(GRand& rand)
{
	size_t outputCount = outputs();
	size_t inputCount = inputs();
	double mag = std::max(0.03, 1.0 / inputCount);
	double* pB = bias().data();
	for(size_t i = 0; i < outputCount; i++)
	{
		*pB = rand.normal() * mag;
		for(size_t j = 0; j < inputCount; j++)
			m_weights[i][j] = rand.normal() * mag;
		pB++;
	}
	pB = biasReverse().data();
	for(size_t i = 0; i < inputCount; i++)
		*(pB++) = rand.normal() * mag;
}

// virtual
void GLayerRestrictedBoltzmannMachine::perturbWeights(GRand& rand, double deviation, size_t start, size_t count)
{
	size_t n = std::min(outputs() - start, count);
	for(size_t i = start; i < n; i++)
		GVec::perturb(m_weights[i].data(), deviation, inputs(), rand);
	GVec::perturb(bias().data() + start, deviation, n, rand);
}

// virtual
void GLayerRestrictedBoltzmannMachine::feedForward(const GVec& in)
{
	activation().copy(bias());
	size_t outputCount = outputs();
	double* pNet = activation().data();
	for(size_t i = 0; i < outputCount; i++)
		*(pNet++) += in.dotProduct(m_weights[i]);
}

// virtual
void GLayerRestrictedBoltzmannMachine::dropOut(GRand& rand, double probOfDrop)
{
	double* pAct = activation().data();
	size_t outputCount = outputs();
	for(size_t i = 0; i < outputCount; i++)
	{
		if(rand.uniform() < probOfDrop)
			pAct[i] = 0.0;
	}
}


void GLayerRestrictedBoltzmannMachine::feedBackward(const GVec& in)
{
	// Feed through the weights
	GVec& n = activationReverse();
	m_weights.multiply(in, n, true);
	n += biasReverse();
}

void GLayerRestrictedBoltzmannMachine::resampleHidden(GRand& rand)
{
	double* pH = activation().data();
	size_t outputCount = outputs();
	for(size_t i = 0; i < outputCount; i++)
	{
		*pH = rand.uniform() < *pH ? 1.0 : 0.0;
		pH++;
	}
}

void GLayerRestrictedBoltzmannMachine::resampleVisible(GRand& rand)
{
	double* pV = activationReverse().data();
	size_t inputCount = inputs();
	for(size_t i = 0; i < inputCount; i++)
	{
		*pV = rand.uniform() < *pV ? 1.0 : 0.0;
		pV++;
	}
}

void GLayerRestrictedBoltzmannMachine::drawSample(GRand& rand, size_t iters)
{
	double* pH = activation().data();
	size_t outputCount = outputs();
	for(size_t i = 0; i < outputCount; i++)
	{
		*pH = ((rand.next() & 1) == 0 ? 0.0 : 1.0);
		pH++;
	}
	for(size_t i = 0; i < iters; i++)
	{
		feedBackward(activation());
		feedForward(activationReverse());
		resampleHidden(rand);
	}
	feedBackward(activation());
}

double GLayerRestrictedBoltzmannMachine::freeEnergy(const GVec& visibleSample)
{
	feedForward(visibleSample);
	GVec& buf = error();
	m_weights.multiply(activationReverse(), buf, false);
	return -activation().dotProduct(buf) -
		biasReverse().dotProduct(activationReverse()) -
		bias().dotProduct(activation());
}

void GLayerRestrictedBoltzmannMachine::contrastiveDivergence(GRand& rand, const GVec& visibleSample, double learningRate, size_t gibbsSamples)
{
	// Details of this implementation were guided by http://axon.cs.byu.edu/~martinez/classes/678/Papers/guideTR.pdf, particularly Sections 3.2 and 3.3.

	// Sample hidden vector
	feedForward(visibleSample);

	// Compute positive gradient
	size_t outputCount = outputs();
	for(size_t i = 0; i < outputCount; i++)
		m_weights[i].addScaled(activation()[i], visibleSample);

	// Add positive gradient to the biases
	biasReverse().addScaled(learningRate, visibleSample);
	bias().addScaled(learningRate, activation());

	// Resample
	for(size_t i = 1; i < gibbsSamples; i++)
	{
		feedBackward(activation());
		feedForward(activationReverse());
		resampleHidden(rand);
	}
	feedBackward(activation());
	feedForward(activationReverse());

	// Compute negative gradient
	for(size_t i = 0; i < outputCount; i++)
		m_weights[i].addScaled(activation()[i], activationReverse());

	// Subtract negative gradient from biases
	biasReverse().addScaled(-learningRate, activationReverse());
	bias().addScaled(-learningRate, activation());
}

void GLayerRestrictedBoltzmannMachine::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GVec& downStreamError = error();
	GVec& upStreamError = pUpStreamLayer->error();
	m_weights.multiply(downStreamError, upStreamError, true);
}

void GLayerRestrictedBoltzmannMachine::updateDeltas(const GVec &upStreamActivation, GVec &deltas)
{
	size_t outputCount = outputs();
	GVec& err = error();
	GVecWrapper delta(deltas.data(), m_weights.cols());
	for(size_t i = 0; i < outputCount; i++)
	{
		delta.vec().addScaled(err[i], upStreamActivation);
		delta.setData(delta.vec().data() + m_weights.cols());
	}
	delta.vec() += err;
}

void GLayerRestrictedBoltzmannMachine::applyDeltas(double learningRate, const GVec &deltas)
{
	size_t outputCount = outputs();
	GConstVecWrapper delta(deltas.data(), m_weights.cols());
	for(size_t i = 0; i < outputCount; i++)
	{
		m_weights[i].addScaled(learningRate, delta.vec());
		delta.setData(delta.vec().data() + m_weights.cols());
	}
	bias().addScaled(learningRate, delta.vec());
}

void GLayerRestrictedBoltzmannMachine::scaleWeights(double factor, bool scaleBiases)
{
	for(size_t i = 0; i < m_weights.rows(); i++)
		m_weights[i] *= factor;
	if(scaleBiases)
		bias() *= factor;
}

void GLayerRestrictedBoltzmannMachine::diminishWeights(double amount, bool diminishBiases)
{
	for(size_t i = 0; i < m_weights.rows(); i++)
		m_weights[i].regularizeL1(amount);
	if(diminishBiases)
		bias().regularizeL1(amount);
}

// virtual
void GLayerRestrictedBoltzmannMachine::maxNorm(double min, double max)
{
	size_t outputCount = outputs();
	for(size_t i = 0; i < outputCount; i++)
	{
		double squaredMag = m_weights[i].squaredMagnitude();
		if(squaredMag > max * max)
		{
			double scal = max / sqrt(squaredMag);
			m_weights[i] *= scal;
		}
		else if(squaredMag < min * min)
		{
			double scal = min / sqrt(squaredMag);
			m_weights[i] *= scal;
		}
	}
}

// virtual
size_t GLayerRestrictedBoltzmannMachine::countWeights()
{
	return (inputs() + 1) * outputs();
}

// virtual
size_t GLayerRestrictedBoltzmannMachine::weightsToVector(double* pOutVector)
{
	memcpy(pOutVector, bias().data(), outputs() * sizeof(double));
	pOutVector += outputs();
	m_weights.toVector(pOutVector);
	return (inputs() + 1) * outputs();
}

// virtual
size_t GLayerRestrictedBoltzmannMachine::vectorToWeights(const double* pVector)
{
	memcpy(bias().data(), pVector, outputs() * sizeof(double));
	pVector += outputs();
	m_weights.fromVector(pVector, inputs());
	return (inputs() + 1) * outputs();
}

// virtual
void GLayerRestrictedBoltzmannMachine::copyWeights(const GNeuralNetLayer* pSource)
{
	GLayerRestrictedBoltzmannMachine* src = (GLayerRestrictedBoltzmannMachine*)pSource;
	m_weights.copyBlock(src->m_weights, 0, 0, INVALID_INDEX, INVALID_INDEX, 0, 0, false);
	bias().copy(src->bias());
}


GLayerConvolutional1D::GLayerConvolutional1D(size_t inputSamples, size_t inputChannels, size_t kernelSize, size_t kernelsPerChannel)
: m_inputSamples(inputSamples),
m_inputChannels(inputChannels),
m_outputSamples(inputSamples - kernelSize + 1),
m_kernelsPerChannel(kernelsPerChannel),
m_kernels(inputChannels * kernelsPerChannel, kernelSize),
m_bias(inputChannels * kernelsPerChannel)
{
	if(kernelSize > inputSamples)
		throw Ex("kernelSize must be <= inputSamples");
	m_activation.resize(2, inputChannels * kernelsPerChannel * m_outputSamples);
}

GLayerConvolutional1D::GLayerConvolutional1D(GDomNode* pNode)
: m_inputSamples((size_t)pNode->field("isam")->asInt()),
m_inputChannels((size_t)pNode->field("ichan")->asInt()),
m_outputSamples((size_t)pNode->field("osam")->asInt()),
m_kernelsPerChannel((size_t)pNode->field("kpc")->asInt()),
m_kernels(pNode->field("kern")),
m_activation(pNode->field("act")),
m_bias(pNode->field("bias"))
{
}

// virtual
GDomNode* GLayerConvolutional1D::serialize(GDom* pDoc)
{
	GDomNode* pNode = baseDomNode(pDoc);
	pNode->addField(pDoc, "isam", pDoc->newInt(m_inputSamples));
	pNode->addField(pDoc, "ichan", pDoc->newInt(m_inputChannels));
	pNode->addField(pDoc, "osam", pDoc->newInt(m_outputSamples));
	pNode->addField(pDoc, "kpc", pDoc->newInt(m_kernelsPerChannel));
	pNode->addField(pDoc, "kern", m_kernels.serialize(pDoc));
	pNode->addField(pDoc, "act", m_activation.serialize(pDoc));
	pNode->addField(pDoc, "bias", m_bias.serialize(pDoc));
	return pNode;
}

// virtual
std::string GLayerConvolutional1D::to_str()
{
	std::ostringstream os;
	os << "[GLayerConvolutional1D:" << GClasses::to_str(inputs()) << "->" << GClasses::to_str(outputs()) << "\n";
	os << " Kernels: " << GClasses::to_str(m_kernels) << "\n";
	os << "]";
	return os.str();
}

// virtual
void GLayerConvolutional1D::resize(size_t inputSize, size_t outputSize)
{
	if(inputSize != m_inputSamples * m_inputChannels)
		throw Ex("Changing the size of GLayerConvolutional1D is not supported");
	if(outputSize != m_inputChannels * m_kernelsPerChannel * m_outputSamples)
		throw Ex("Changing the size of GLayerConvolutional1D is not supported");
}

// virtual
void GLayerConvolutional1D::resetWeights(GRand& rand)
{
	size_t kernelSize = m_kernels.cols();
	double mag = std::max(0.03, 1.0 / kernelSize);
	for(size_t i = 0; i < m_kernels.rows(); i++)
		m_kernels[i].fillNormal(rand, mag);
	bias().fillNormal(rand, mag);
}

// virtual
void GLayerConvolutional1D::feedForward(const GVec& in)
{
	// Copy bias to net
	for(size_t i = 0; i < m_outputSamples; i++)
		activation().put(bias().size() * i, bias());

	// Feed in through
	size_t kernelSize = m_kernels.cols();
	GVec& n = activation();
	size_t netPos = 0;
	size_t inPos = 0;
	for(size_t i = 0; i < m_outputSamples; i++) // for each output sample...
	{
		size_t kern = 0;
		for(size_t j = 0; j < m_inputChannels; j++) // for each input channel...
		{
			for(size_t k = 0; k < m_kernelsPerChannel; k++) // for each kernel...
			{
				GVec& w = m_kernels[kern++];
				double d = 0.0;
				for(size_t l = 0; l < kernelSize; l++) // for each connection...
					d += w[l] * in[inPos + l * m_inputChannels];
				n[netPos++] += d;
			}
			inPos++;
		}
	}
}

// virtual
void GLayerConvolutional1D::dropOut(GRand& rand, double probOfDrop)
{
	GVec& a = activation();
	size_t outputCount = outputs();
	for(size_t i = 0; i < outputCount; i++)
	{
		if(rand.uniform() < probOfDrop)
			a[i] = 0.0;
	}
}

// virtual
void GLayerConvolutional1D::dropConnect(GRand& rand, double probOfDrop)
{
	throw Ex("Sorry, convolutional layers do not support dropConnect");
}

// virtual
void GLayerConvolutional1D::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GAssert(pUpStreamLayer->outputs() == inputs());
	GVec& upStreamErr = pUpStreamLayer->error();
	GVec& downStreamErr = error();
	size_t kernelSize = m_kernels.cols();
	upStreamErr.fill(0.0);
	size_t upPos = 0;
	size_t downPos = 0;
	for(size_t i = 0; i < m_outputSamples; i++) // for each sample...
	{
		size_t kern = 0;
		for(size_t j = 0; j < m_inputChannels; j++) // for each input channel...
		{
			for(size_t k = 0; k < m_kernelsPerChannel; k++) // for each kernel...
			{
				GVec& w = m_kernels[kern++];
				size_t samp = 0;
				for(size_t l = 0; l < kernelSize; l++) // for each connection...
				{
					upStreamErr[upPos + samp] += w[l] * downStreamErr[downPos];
					samp += m_inputChannels;
				}
				downPos++;
			}
			upPos++;
		}
	}
}

void GLayerConvolutional1D::updateDeltas(const GVec &upStreamActivation, GVec &deltas)
{
	GVec& err = error();
	size_t kernelSize = m_kernels.cols();
	size_t errPos = 0;
	size_t upPos = 0;
	for(size_t i = 0; i < m_outputSamples; i++) // for each sample...
	{
		double *delta = deltas.data();
		for(size_t j = 0; j < m_inputChannels; j++) // for each input channel...
		{
			for(size_t k = 0; k < m_kernelsPerChannel; k++) // for each kernel...
			{
				size_t upOfs = 0;
				for(size_t l = 0; l < kernelSize; l++) // for each connection...
				{
					*delta++ += err[errPos] * upStreamActivation[upPos + upOfs];
					upOfs += m_inputChannels;
				}
				*delta++ += err[errPos++];
			}
			upPos++;
		}
	}
}

void GLayerConvolutional1D::applyDeltas(double learningRate, const GVec &deltas)
{
	size_t kernelSize = m_kernels.cols();
	size_t errPos = 0;
	size_t upPos = 0;
	size_t kern = 0;
	const double *delta = deltas.data();
	for(size_t j = 0; j < m_inputChannels; j++) // for each input channel...
	{
		for(size_t k = 0; k < m_kernelsPerChannel; k++) // for each kernel...
		{
			GVec& d = m_kernels[kern];
			size_t upOfs = 0;
			for(size_t l = 0; l < kernelSize; l++) // for each connection...
			{
				d[l] += learningRate * *delta++;
				upOfs += m_inputChannels;
			}
			bias()[kern++] += learningRate * *delta++;
			errPos++;
		}
		upPos++;
	}
}

// virtual
void GLayerConvolutional1D::scaleWeights(double factor, bool scaleBiases)
{
	for(size_t i = 0; i < m_kernels.rows(); i++)
		m_kernels[i] *= factor;
	if(scaleBiases)
		bias() *= factor;
}

// virtual
void GLayerConvolutional1D::diminishWeights(double amount, bool diminishBiases)
{
	for(size_t i = 0; i < m_kernels.rows(); i++)
		m_kernels[i].regularizeL1(amount);
	if(diminishBiases)
		bias().regularizeL1(amount);
}

// virtual
size_t GLayerConvolutional1D::countWeights()
{
	return (m_kernels.rows() + 1) * m_kernels.cols();
}

// virtual
size_t GLayerConvolutional1D::weightsToVector(double* pOutVector)
{
	memcpy(pOutVector, bias().data(), m_kernels.rows() * sizeof(double));
	pOutVector += m_kernels.rows();
	m_kernels.toVector(pOutVector);
	return (m_kernels.rows() + 1) * m_kernels.cols();
}

// virtual
size_t GLayerConvolutional1D::vectorToWeights(const double* pVector)
{
	memcpy(bias().data(), pVector, m_kernels.rows() * sizeof(double));
	pVector += m_kernels.rows();
	m_kernels.fromVector(pVector, m_kernels.rows());
	return (m_kernels.rows() + 1) * m_kernels.cols();
}

// virtual
void GLayerConvolutional1D::copyWeights(const GNeuralNetLayer* pSource)
{
	GLayerConvolutional1D* src = (GLayerConvolutional1D*)pSource;
	m_kernels.copyBlock(src->m_kernels, 0, 0, INVALID_INDEX, INVALID_INDEX, 0, 0, false);
	bias().copy(src->bias());
}

// virtual
void GLayerConvolutional1D::perturbWeights(GRand& rand, double deviation, size_t start, size_t count)
{
	if(start != 0)
		throw Ex("Sorry, convolutional layers do not support perturbing weights for a subset of units");
	size_t kernelSize = m_kernels.cols();
	for(size_t i = 0; i < m_kernels.rows(); i++)
		GVec::perturb(m_kernels[i].data(), deviation, kernelSize, rand);
	GVec::perturb(bias().data(), deviation, m_kernels.rows(), rand);
}

// virtual
void GLayerConvolutional1D::maxNorm(double min, double max)
{
	for(size_t i = 0; i < m_kernels.rows(); i++)
		m_kernels[i].clip(-max, max);
}







size_t GLayerConvolutional2D::Image::npos = (size_t) -1;

GLayerConvolutional2D::Image::Image(GVec *_data, size_t _width, size_t _height, size_t _channels)
: data(_data), width(_width), height(_height), channels(_channels), interlaced(true), dx(0), dy(0), dz(0), px(0), py(0), sx(1), sy(1), invertStride(false), flip(false) {}

size_t GLayerConvolutional2D::Image::index(size_t x, size_t y, size_t z) const
{
	z += dz;

	if(invertStride)
	{
		if((x + dx) % sx > 0 || (y + dy) % sy > 0)
			return npos;
		x = (x + dx) / sx - px;
		y = (y + dy) / sy - py;
	}
	else
	{
		x += dx * sx - px;
		y += dy * sy - py;
	}

	if(flip)
	{
		x = width - x - 1;
		y = height - y - 1;
	}

	if(x >= width || y >= height)
		return npos;

	if(interlaced)
		return (y * width + x) * channels + z;
	else
		return (z * height + y) * width + x;
}

double GLayerConvolutional2D::Image::read(size_t x, size_t y, size_t z) const
{
	size_t i = index(x, y, z);
	if(i == npos)
		return 0.0;
	else
		return (*data)[i];
}

double &GLayerConvolutional2D::Image::at(size_t x, size_t y, size_t z)
{
	size_t i = index(x, y, z);
	if(i == npos)
		throw Ex("tried to access invalid image location!");
	else
		return (*data)[i];
}


size_t GLayerConvolutional2D::none = (size_t) -1;

GLayerConvolutional2D::GLayerConvolutional2D(size_t width, size_t height, size_t channels, size_t kWidth, size_t kHeight, size_t kCount)
: m_width(width), m_height(height), m_channels(channels),
  m_kWidth(kWidth), m_kHeight(kHeight),
  m_outputWidth(width - kWidth + 1), m_outputHeight(height - kHeight + 1),
  m_bias(kCount),
  m_kernels(kCount, kWidth * kHeight * channels),
  m_activation(2, m_outputWidth * m_outputHeight * kCount),
  m_kernelImage(NULL, kWidth, kHeight, channels), m_deltaImage(NULL, kWidth, kHeight, channels),
  m_inputImage(NULL, width, height, channels), m_upStreamErrorImage(NULL, width, height, channels),
  m_actImage(&m_activation[0], m_outputWidth, m_outputHeight, kCount), m_errImage(&m_activation[1], m_outputWidth, m_outputHeight, kCount)
{}

GLayerConvolutional2D::GLayerConvolutional2D(size_t kWidth, size_t kHeight, size_t kCount)
: m_width(FLEXIBLE_SIZE), m_height(FLEXIBLE_SIZE), m_channels(FLEXIBLE_SIZE),
  m_kWidth(kWidth), m_kHeight(kHeight),
  m_outputWidth(0), m_outputHeight(0),
  m_bias(kCount),
  m_kernels(kCount, 0),
  m_activation(2, 0),
  m_kernelImage(NULL, kWidth, kHeight, 0), m_deltaImage(NULL, kWidth, kHeight, 0),
  m_inputImage(NULL, 0, 0, 0), m_upStreamErrorImage(NULL, 0, 0, 0),
  m_actImage(&m_activation[0], 0, 0, 0), m_errImage(&m_activation[1], 0, 0, 0)
{}

GLayerConvolutional2D::GLayerConvolutional2D(GDomNode* pNode)
: m_width(pNode->field("width")->asInt()), m_height(pNode->field("height")->asInt()), m_channels(pNode->field("channels")->asInt()),
  m_kWidth(pNode->field("kWidth")->asInt()), m_kHeight(pNode->field("kHeight")->asInt()),
  m_outputWidth(pNode->field("outputWidth")->asInt()), m_outputHeight(pNode->field("outputHeight")->asInt()),
  m_bias(pNode->field("bias")),
  m_kernels(pNode->field("kernels")),
  m_activation(2, m_outputWidth * m_outputHeight * m_kernels.rows()),
  m_kernelImage(NULL, m_kWidth, m_kHeight, m_channels), m_deltaImage(NULL, m_kWidth, m_kHeight, m_channels),
  m_inputImage(NULL, m_width, m_height, m_channels), m_upStreamErrorImage(NULL, m_width, m_height, m_channels),
  m_actImage(&m_activation[0], m_outputWidth, m_outputHeight, m_kernels.rows()), m_errImage(&m_activation[1], m_outputWidth, m_outputHeight, m_kernels.rows())
{
	m_inputImage.sx	= pNode->field("strideX")->asInt();
	m_inputImage.sy	= pNode->field("strideY")->asInt();
	m_inputImage.px	= pNode->field("paddingX")->asInt();
	m_inputImage.py	= pNode->field("paddingY")->asInt();

	setInputInterlaced(pNode->field("inputInterlaced")->asBool());
	setKernelsInterlaced(pNode->field("kernelsInterlaced")->asBool());
	setOutputInterlaced(pNode->field("outputInterlaced")->asBool());
}

GDomNode *GLayerConvolutional2D::serialize(GDom *pDoc)
{
	GDomNode *pNode = baseDomNode(pDoc);
	pNode->addField(pDoc, "width", pDoc->newInt(m_width));
	pNode->addField(pDoc, "height", pDoc->newInt(m_height));
	pNode->addField(pDoc, "channels", pDoc->newInt(m_channels));
	pNode->addField(pDoc, "kWidth", pDoc->newInt(m_kWidth));
	pNode->addField(pDoc, "kHeight", pDoc->newInt(m_kHeight));
	pNode->addField(pDoc, "strideX", pDoc->newInt(m_inputImage.sx));
	pNode->addField(pDoc, "strideY", pDoc->newInt(m_inputImage.sy));
	pNode->addField(pDoc, "paddingX", pDoc->newInt(m_inputImage.px));
	pNode->addField(pDoc, "paddingY", pDoc->newInt(m_inputImage.py));
	pNode->addField(pDoc, "outputWidth", pDoc->newInt(m_outputWidth));
	pNode->addField(pDoc, "outputHeight", pDoc->newInt(m_outputHeight));
	pNode->addField(pDoc, "inputInterlaced", pDoc->newBool(m_inputImage.interlaced));
	pNode->addField(pDoc, "kernelsInterlaced", pDoc->newBool(m_kernelImage.interlaced));
	pNode->addField(pDoc, "outputInterlaced", pDoc->newBool(m_actImage.interlaced));
	pNode->addField(pDoc, "bias", m_bias.serialize(pDoc));
	pNode->addField(pDoc, "kernels", m_kernels.serialize(pDoc));
	return pNode;

}

std::string GLayerConvolutional2D::to_str()
{
	std::stringstream ss;
	ss << "[GLayerConvolutional2D:\n"
	   << "    " << m_width << "x" << m_height << "x" << m_channels << " (stride=" << m_inputImage.sx << "," << m_inputImage.sy << "; padding=" << m_inputImage.px << "," << m_inputImage.py << ")\n"
	   << " *  " << m_kWidth << "x" << m_kHeight << "\n"
	   << " -> " << m_outputWidth << "x" << m_outputHeight << "x" << m_kernels.rows() << "\n"
	   << "]";
	return ss.str();
}

void GLayerConvolutional2D::resize(size_t inputSize, size_t outputSize)
{
	if(inputSize != inputs() || outputSize != outputs())
		throw Ex("GLayerConvolutional2D can only be resized given an upstream convolutional layer!");
}

void GLayerConvolutional2D::resizeInputs(GNeuralNetLayer *pUpStreamLayer)
{
	if(pUpStreamLayer->type() != layer_convolutional2d)
		throw Ex("GLayerConvolutional2D can only be resized given an upstream convolutional layer!");

	GLayerConvolutional2D &upstream = *((GLayerConvolutional2D *) pUpStreamLayer);

	m_width			= upstream.outputWidth();
	m_height		= upstream.outputHeight();
	m_channels		= upstream.outputChannels();

	m_kernels.resize(m_kernels.rows(), m_kWidth * m_kHeight * m_channels);

	m_bias.fill(0.0);
	m_kernels.fill(0.0);

	m_inputImage.width = m_width;
	m_inputImage.height = m_height;
	m_inputImage.channels = m_channels;

	m_upStreamErrorImage.width = m_width;
	m_upStreamErrorImage.height = m_height;
	m_upStreamErrorImage.channels = m_channels;

	m_kernelImage.channels = m_channels;
	m_deltaImage.channels = m_channels;

	updateOutputSize();
}

void GLayerConvolutional2D::feedForward(const GVec &in)
{
	m_inputImage.data = const_cast<GVec *>(&in);

	Image &n = m_actImage;
	n.data->fill(0.0);
	for(n.dz = 0; n.dz < n.channels; ++n.dz)
	{
		m_kernelImage.data = &m_kernels[n.dz];
		convolve(m_inputImage, m_kernelImage, n);
		for(size_t y = 0; y < n.height; ++y)
			for(size_t x = 0; x < n.width; ++x)
				n.at(x, y) += m_bias[n.dz];
	}
	n.dz = 0;
}

void GLayerConvolutional2D::dropOut(GRand &rand, double probOfDrop)
{
	throw Ex("dropOut not implemented");
}

void GLayerConvolutional2D::dropConnect(GRand &rand, double probOfDrop)
{
	throw Ex("dropConnect not implemented");
}

void GLayerConvolutional2D::backPropError(GNeuralNetLayer *pUpStreamLayer)
{
	Image &err = m_errImage;
	Image &upErr = m_upStreamErrorImage;

	upErr.data = &pUpStreamLayer->error();
	upErr.data->fill(0.0);
	upErr.px = m_inputImage.px;
	upErr.py = m_inputImage.py;

	err.invertStride = true, err.sx = m_inputImage.sx, err.sy = m_inputImage.sy;
	for(upErr.dz = 0; upErr.dz < upErr.channels; ++upErr.dz)
	{
		for(err.dz = 0; err.dz < err.channels; ++err.dz)
		{
			m_kernelImage.data = &m_kernels[err.dz];
			m_kernelImage.flip = true, m_kernelImage.dz = upErr.dz;
			convolveFull(err, m_kernelImage, upErr, 1);
			m_kernelImage.flip = false, m_kernelImage.dz = 0;
		}
	}
	err.sx = err.sy = 1, err.invertStride = false;
	err.dz = 0;
	upErr.dz = 0;
	upErr.px = upErr.py = 0;
}

void GLayerConvolutional2D::updateDeltas(const GVec &upStreamActivation, GVec &deltas)
{
	Image &err = m_errImage;
	Image &in = m_inputImage;
	in.data = const_cast<GVec *>(&upStreamActivation);
	size_t count = m_kernels.cols();
	GVecWrapper delta(deltas.data(), count);
	m_deltaImage.data = &delta.vec();
	for(err.dz = 0; err.dz < err.channels; ++err.dz)
	{
		double *biasDelta = m_deltaImage.data->data() + count;
		m_deltaImage.data->fill(0.0);
		for(in.dz = m_deltaImage.dz = 0; in.dz < in.channels; ++in.dz, ++m_deltaImage.dz)
			for(in.dy = 0; in.dy < err.height; ++in.dy)
				for(in.dx = 0; in.dx < err.width; ++in.dx)
				{
					addScaled(in, err.read(in.dx, in.dy), m_deltaImage);
					*biasDelta += err.read(in.dx, in.dy);
				}
		m_deltaImage.dz = 0;
		delta.setData(delta.vec().data() + count + 1);
	}
	in.dz = 0;
}

void GLayerConvolutional2D::applyDeltas(double learningRate, const GVec &deltas)
{
	size_t count = m_kernels.cols();
	GConstVecWrapper delta(deltas.data(), count);
	for(size_t i = 0; i < m_kernels.rows(); i++)
	{
		m_kernels[i].addScaled(learningRate, delta.vec());
		m_bias[i] += learningRate * *(delta.vec().data() + count);
		delta.setData(delta.vec().data() + count + 1);
	}
}

void GLayerConvolutional2D::scaleWeights(double factor, bool scaleBiases)
{
	throw Ex("scaleWeights not implemented");
}

void GLayerConvolutional2D::diminishWeights(double amount, bool regularizeBiases)
{
	throw Ex("diminishWeights not implemented");
}

size_t GLayerConvolutional2D::countWeights()
{
	return m_kWidth * m_kHeight * m_channels * m_kernels.rows() + m_kernels.rows();
}

size_t GLayerConvolutional2D::weightsToVector(double *pOutVector)
{
	m_kernels.toVector(pOutVector);
	GVecWrapper(pOutVector + m_kernels.rows() * m_kernels.cols(), m_kernels.rows()).vec().put(0, m_bias);
	return countWeights();
}

size_t GLayerConvolutional2D::vectorToWeights(const double *pVector)
{
	m_kernels.fromVector(pVector, m_kernels.rows());
	m_bias.put(0, GConstVecWrapper(pVector + m_kernels.rows() * m_kernels.cols(), m_kernels.rows()).vec());
	return countWeights();
}

void GLayerConvolutional2D::copyWeights(const GNeuralNetLayer *pSource)
{
	throw Ex("copyWeights not implemented");
}

void GLayerConvolutional2D::resetWeights(GRand &rand)
{
	double mag = std::max(0.03, 1.0 / (m_outputWidth * m_outputHeight * m_kernels.rows()));
	for(size_t i = 0; i < m_kernels.rows(); i++)
		m_kernels[i].fillNormal(rand, mag);
	m_bias.fillNormal(rand, mag);
}

void GLayerConvolutional2D::perturbWeights(GRand &rand, double deviation, size_t start, size_t count)
{
	GAssert(start + count < m_kernels.rows());
	size_t n = std::min(m_kernels.rows() - start, count);
	for(size_t j = start; j < n; j++)
		GVec::perturb(m_kernels[j].data(), deviation, m_kernels.cols(), rand);
	GVec::perturb(m_bias.data(), deviation, m_kernels.rows(), rand);
}

void GLayerConvolutional2D::maxNorm(double min, double max)
{
	throw Ex("maxNorm not implemented");
}

void GLayerConvolutional2D::setPadding(size_t px, size_t py)
{
	m_inputImage.px = px;
	m_inputImage.py = (py == none ? px : py);
	updateOutputSize();
}

void GLayerConvolutional2D::setStride(size_t sx, size_t sy)
{
	m_inputImage.sx = sx;
	m_inputImage.sy = (sy == none ? sx : sy);
	updateOutputSize();
}

void GLayerConvolutional2D::setInterlaced(bool interlaced)
{
	setInputInterlaced(interlaced);
	setKernelsInterlaced(interlaced);
	setOutputInterlaced(interlaced);
}

void GLayerConvolutional2D::setInputInterlaced(bool interlaced)
{
	m_inputImage.interlaced = interlaced;
	m_upStreamErrorImage.interlaced = interlaced;
}

void GLayerConvolutional2D::setKernelsInterlaced(bool interlaced)
{
	m_kernelImage.interlaced = interlaced;
	m_deltaImage.interlaced = interlaced;
}

void GLayerConvolutional2D::setOutputInterlaced(bool interlaced)
{
	m_actImage.interlaced = interlaced;
	m_errImage.interlaced = interlaced;
}

void GLayerConvolutional2D::addKernel()
{
	m_kernels.resize(m_kernels.rows() + 1, m_kernels.cols());

	GVec temp(m_bias);
	m_bias.resize(m_kernels.rows() + 1);
	m_bias.put(0, temp);

	m_actImage.channels = m_kernels.rows();
	m_errImage.channels = m_kernels.rows();
	updateOutputSize();
}

void GLayerConvolutional2D::addKernels(size_t n)
{
	for(size_t i = 0; i < n; ++i)
		addKernel();
}

double GLayerConvolutional2D::filterSum(const Image &in, const Image &filter, size_t channels)
{
	double output = 0.0;
	for(size_t z = 0; z < channels; ++z)
		for(size_t y = 0; y < filter.height; ++y)
			for(size_t x = 0; x < filter.width; ++x)
				output += in.read(x, y, z) * filter.read(x, y, z);
	return output;
}

void GLayerConvolutional2D::addScaled(const Image &in, double scalar, Image &out)
{
	for(size_t y = 0; y < out.height; ++y)
		for(size_t x = 0; x < out.width; ++x)
			out.at(x, y) += in.read(x, y) * scalar;
}

void GLayerConvolutional2D::convolve(const Image &in, const Image &filter, Image &out, size_t channels)
{
	size_t x, y;
	if(channels == none)
		channels = filter.channels;
	for(y = 0, in.dy = out.py; y < out.height; ++y, ++in.dy)
		for(x = 0, in.dx = out.px; x < out.width; ++x, ++in.dx)
			out.at(in.dx, in.dy, 0) += filterSum(in, filter, channels);
	in.dx = in.dy = 0;
}

void GLayerConvolutional2D::convolveFull(const Image &in, const Image &filter, Image &out, size_t channels)
{
	size_t px = in.px, py = in.py;
	in.px = (in.px + filter.width - 1) / in.sx, in.py = (in.py + filter.height - 1) / in.sy;
	convolve(in, filter, out, channels);
	in.px = px, in.py = py;
}

void GLayerConvolutional2D::updateOutputSize()
{
	m_outputWidth = (m_width - m_kWidth + 2 * m_inputImage.px) / m_inputImage.sx + 1;
	m_outputHeight = (m_height - m_kHeight + 2 * m_inputImage.py) / m_inputImage.sy + 1;
	m_activation.resize(2, m_outputWidth * m_outputHeight * m_kernels.rows());

	m_actImage.data = &m_activation[0];
	m_actImage.width = m_outputWidth;
	m_actImage.height = m_outputHeight;

	m_errImage.data = &m_activation[1];
	m_errImage.width = m_outputWidth;
	m_errImage.height = m_outputHeight;
}
















GMaxPooling2D::GMaxPooling2D(size_t inputCols, size_t inputRows, size_t inputChannels, size_t regionSize)
: m_inputCols(inputCols),
m_inputRows(inputRows),
m_inputChannels(inputChannels)
{
	if(inputCols % regionSize != 0)
		throw Ex("inputCols is not a multiple of regionSize");
	if(inputRows % regionSize != 0)
		throw Ex("inputRows is not a multiple of regionSize");
	m_activation.resize(2, m_inputRows * m_inputCols * m_inputChannels / (m_regionSize * m_regionSize));
}

GMaxPooling2D::GMaxPooling2D(GDomNode* pNode)
: m_inputCols((size_t)pNode->field("icol")->asInt()),
m_inputRows((size_t)pNode->field("irow")->asInt()),
m_inputChannels((size_t)pNode->field("ichan")->asInt()),
m_regionSize((size_t)pNode->field("size")->asInt())
{
}

GMaxPooling2D::~GMaxPooling2D()
{
}

// virtual
GDomNode* GMaxPooling2D::serialize(GDom* pDoc)
{
	GDomNode* pNode = baseDomNode(pDoc);
	pNode->addField(pDoc, "icol", pDoc->newInt(m_inputCols));
	pNode->addField(pDoc, "irow", pDoc->newInt(m_inputRows));
	pNode->addField(pDoc, "ichan", pDoc->newInt(m_inputChannels));
	pNode->addField(pDoc, "size", pDoc->newInt(m_regionSize));
	return pNode;
}

// virtual
std::string GMaxPooling2D::to_str()
{
	std::ostringstream os;
	os << "[GMaxPooling2D:" << GClasses::to_str(inputs()) << "->" << GClasses::to_str(outputs()) << "]";
	return os.str();
}

// virtual
void GMaxPooling2D::resize(size_t inputSize, size_t outputSize)
{
	if(inputSize != m_inputCols * m_inputRows * m_inputChannels)
		throw Ex("Changing the size of GMaxPooling2D is not supported");
	if(outputSize != m_inputChannels * m_inputCols * m_inputRows / (m_regionSize * m_regionSize))
		throw Ex("Changing the size of GMaxPooling2D is not supported");
}

// virtual
void GMaxPooling2D::feedForward(const GVec& in)
{
	GVec& a = activation();
	size_t actPos = 0;
	for(size_t yy = 0; yy < m_inputRows; yy += m_regionSize)
	{
		for(size_t xx = 0; xx < m_inputCols; xx += m_regionSize)
		{
			for(size_t c = 0; c < m_inputChannels; c++)
			{
				double m = -1e100;
				size_t yStep = m_inputCols * m_inputChannels;
				size_t yStart = yy * yStep;
				size_t yEnd = yStart + m_regionSize * yStep;
				for(size_t y = yStart; y < yEnd; y += yStep)
				{
					size_t xStart = yStart + xx * m_inputChannels + c;
					size_t xEnd = xStart + m_regionSize * m_inputChannels + c;
					for(size_t x = xStart; x < xEnd; x += m_inputChannels)
						m = std::max(m, in[x]);
				}
				a[actPos++] = m;
			}
		}
	}
}

// virtual
void GMaxPooling2D::backPropError(GNeuralNetLayer* pUpStreamLayer)
{
	GVec& downStreamErr = error();
	GVec& a = pUpStreamLayer->activation();
	GVec& upStreamErr = pUpStreamLayer->error();
	size_t downPos = 0;
	for(size_t yy = 0; yy < m_inputRows; yy += m_regionSize)
	{
		for(size_t xx = 0; xx < m_inputCols; xx += m_regionSize)
		{
			for(size_t c = 0; c < m_inputChannels; c++)
			{
				double m = -1e100;
				size_t maxIndex = 0;
				size_t yStep = m_inputCols * m_inputChannels;
				size_t yStart = yy * yStep;
				size_t yEnd = yStart + m_regionSize * yStep;
				for(size_t y = yStart; y < yEnd; y += yStep)
				{
					size_t xStart = yStart + xx * m_inputChannels + c;
					size_t xEnd = xStart + m_regionSize * m_inputChannels + c;
					for(size_t x = xStart; x < xEnd; x += m_inputChannels)
					{
						if(a[x] > m)
						{
							m = a[x];
							maxIndex = x;
						}
						upStreamErr[x] = 0.0;
					}
				}
				upStreamErr[maxIndex] = downStreamErr[downPos++];
			}
		}
	}
}

// virtual
size_t GMaxPooling2D::countWeights()
{
	return 0;
}






} // namespace GClasses
