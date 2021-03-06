/*
  The contents of this file are dedicated by all of its authors, including

    Michael S. Gashler,
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

#ifndef __GLAYER_H__
#define __GLAYER_H__

#include <vector>
#include "GMatrix.h"
#include <ostream>

namespace GClasses {

/// This value is used for the number of inputs or outputs of a neural net layer when
/// you do not wish to specify a fixed size. For example, it may be used for the inputs
/// of the first layer or the outputs of the last layer, because the training data will
/// provide these sizes. (In fact, those ends will be resized to fit the data whether or
/// not FLEXIBLE_SIZE is used.) FLEXIBLE_SIZE should probably not be used on an end that
/// will be connected to another end with FLEXIBLE_SIZE because then both ends will stay
/// at a size of zero, which will result in approximately baseline predictions.
#define FLEXIBLE_SIZE (size_t)0

class GActivationFunction;

/// Represents a layer of neurons in a neural network
class GNeuralNetLayer
{
public:
	enum LayerType
	{
		layer_tanh,
		layer_logistic,
		layer_bentidentity,
		layer_softroot,
		layer_sigexp,
		layer_gaussian,
		layer_sine,
		layer_rectifier,
		layer_leakyrectifier,
		layer_softplus,
		layer_linear,
		layer_activation,
		layer_productpooling,
		layer_additionpooling,
		layer_maxout,
		layer_softmax,
		layer_restrictedboltzmannmachine,
		layer_convolutional1d,
		layer_convolutional2d,
		layer_maxpooling,
	};
	
	GNeuralNetLayer() {}
	virtual ~GNeuralNetLayer() {}

	/// Returns the type of this layer
	virtual LayerType type() const = 0;

	/// Returns true iff this layer has weights
	virtual bool hasWeights() const { return false; }

	/// Returns true iff this layer operates only on individual elements
	virtual bool elementWise() const { return false; }

	/// Returns true iff this layer does its computations in parallel on a GPU.
	virtual bool usesGPU() { return false; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) = 0;

	/// Unmarshalls the specified DOM node into a layer object.
	static GNeuralNetLayer* deserialize(GDomNode* pNode);

	/// Makes a string representation of this layer
	virtual std::string to_str() = 0;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const = 0;

	/// Returns the number of values that this layer outputs.
	virtual size_t outputs() const = 0;

	/// Resizes this layer. If pRand is non-NULL, then it preserves existing weights when possible
	/// and initializes any others to small random values.
	virtual void resize(size_t inputs, size_t outputs) = 0;

	/// Resizes the inputs of this layer (as in the above function) given the upstream layer to calculate needed inputs.
	virtual void resizeInputs(GNeuralNetLayer* pUpStreamLayer)
	{
		resize(pUpStreamLayer->outputs(), outputs());
	}

	/// Returns a buffer where the activation from the most-recent call to feedForward is stored.
	virtual GVec& activation() = 0;

	/// Returns a buffer where the error terms for each unit are stored.
	virtual GVec& error() = 0;

	/// Feeds an input vector through this layer to compute the output of this layer.
	virtual void feedForward(const GVec& in) = 0;

	/// Feeds the activation of the previous layer through this layer to compute the output of this layer.
	virtual void feedForward(GNeuralNetLayer* pUpStreamLayer)
	{
		feedForward(pUpStreamLayer->activation());
	}

	/// Computes the activation error of the layer that feeds into this one.
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) = 0;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() = 0;

	/// Feeds a matrix through this layer, one row at-a-time, and returns the resulting transformed matrix.
	GMatrix* feedThrough(const GMatrix& data);

protected:
	GDomNode* baseDomNode(GDom* pDoc);
};


/// The base class of layers that have weights
class GParameterizedLayer : public GNeuralNetLayer
{
public:
	GParameterizedLayer() {}
	virtual ~GParameterizedLayer() {}

	/// Returns true iff this layer has weights
	virtual bool hasWeights() const { return true; }

	/// \deprecated Randomly sets the activation of some units to 0.
	virtual void dropOut(GRand& rand, double probOfDrop) = 0;

	/// Serialize the weights in this layer into a vector. Return the number of elements written.
	virtual size_t weightsToVector(double* pOutVector) = 0;

	/// Deserialize from a vector to the weights in this layer. Return the number of elements consumed.
	virtual size_t vectorToWeights(const double* pVector) = 0;

	/// Copy the weights from pSource to this layer. (Assumes pSource is the same type of layer.)
	virtual void copyWeights(const GNeuralNetLayer* pSource) = 0;

	/// Initialize the weights with small random values.
	virtual void resetWeights(GRand& rand) = 0;

	/// Perturbs the weights that feed into the specifed units with Gaussian noise. The
	/// default values apply the perturbation to all units.
	virtual void perturbWeights(GRand& rand, double deviation, size_t start = 0, size_t count = INVALID_INDEX) = 0;

	/// Scales weights if necessary such that the manitude of the weights (not including the bias) feeding into each unit are >= min and <= max.
	virtual void maxNorm(double min, double max) = 0;

	/// Multiplies all the weights by the specified factor.
	virtual void scaleWeights(double factor, bool scaleBiases) = 0;

	/// Moves all weights in the direction of zero by the specified amount.
	virtual void diminishWeights(double amount, bool regularizeBiases) = 0;

	/// Updates the deltas for updating the weights by gradient descent.
	/// (Assumes the error has already been computed and deactivated.)
	virtual void updateDeltas(const GVec& upStreamActivation, GVec &deltas) = 0;

	/// Add the weight and bias deltas to the weights.
	virtual void applyDeltas(double learningRate, const GVec &deltas) = 0;
};



/// Standard fully-connected layer of weights. Often followed by a GLayerActivation.
class GLayerLinear : public GParameterizedLayer
{
protected:
	GMatrix m_weights; // An (inputs+1)-by-outputs matrix of weights. The last row contains the bias values.
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.

public:
	GLayerLinear(size_t outputs);
	GLayerLinear(size_t inputs, size_t outputs);
	GLayerLinear(GDomNode* pNode);

	/// Returns the type of this layer
	virtual LayerType type() const override { return layer_linear; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const override { return m_weights.rows() - 1; }

	/// Returns the number of values that this layer outputs.
	virtual size_t outputs() const override { return m_weights.cols(); }

	/// Resizes this layer. If pRand is non-NULL, then it preserves existing weights when possible
	/// and initializes any others to small random values.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Returns a buffer where the activation from the most-recent call to feedForward is stored.
	virtual GVec& activation() override { return m_activation[0]; }

	/// Returns a buffer where the error terms for each unit are stored.
	virtual GVec& error() override { return m_activation[1]; }

	/// \deprecated Randomly sets the activation of some units to 0.
	virtual void dropOut(GRand& rand, double probOfDrop) override { throw Ex("GLayerLinear::dropOut is deprecated; use a drop out layer instead"); }

	/// Feeds an input vector through this layer to compute the output of this layer.
	virtual void feedForward(const GVec& in) override;

	/// Computes the activation error of the layer that feeds into this one.
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Updates the deltas for updating the weights by gradient descent.
	/// (Assumes the error has already been computed and deactivated.)
	virtual void updateDeltas(const GVec& upStreamActivation, GVec &deltas) override;

	/// Add the weight and bias deltas to the weights.
	virtual void applyDeltas(double learningRate, const GVec &deltas) override;

	/// Multiplies all the weights by the specified factor.
	virtual void scaleWeights(double factor, bool scaleBiases) override;

	/// Moves all weights in the direction of zero by the specified amount.
	virtual void diminishWeights(double amount, bool regularizeBiases) override;

	/// Applies contractive regularization to the weights in this layer.
	void contractWeights(double factor, bool contractBiases);

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;

	/// Serialize the weights in this layer into a vector. Return the number of elements written.
	virtual size_t weightsToVector(double* pOutVector) override;

	/// Deserialize from a vector to the weights in this layer. Return the number of elements consumed.
	virtual size_t vectorToWeights(const double* pVector) override;

	/// Copy the weights from pSource to this layer. (Assumes pSource is the same type of layer.)
	virtual void copyWeights(const GNeuralNetLayer* pSource) override;

	/// Initialize the weights with small random values.
	virtual void resetWeights(GRand& rand) override;

	/// Perturbs the weights that feed into the specifed units with Gaussian noise. The
	/// default values apply the perturbation to all units.
	virtual void perturbWeights(GRand& rand, double deviation, size_t start = 0, size_t count = INVALID_INDEX) override;

	/// Scales weights if necessary such that the manitude of the weights (not including the bias) feeding into each unit are >= min and <= max.
	virtual void maxNorm(double min, double max) override;

	/// Returns the bias vector of this layer.
	GVec& bias() { return m_weights.back(); }

	/// Returns the bias vector of this layer.
	const GVec& bias() const { return m_weights.back(); }

	/// Get the entire weights matrix
	GMatrix &weights() { return m_weights; }

	/// Get the entire weights matrix
	const GMatrix &weights() const { return m_weights; }

	/// Transforms the weights of this layer by the specified transformation matrix and offset vector.
	/// transform should be the pseudoinverse of the transform applied to the inputs. pOffset should
	/// be the negation of the offset added to the inputs after the transform, or the transformed offset
	/// that is added before the transform.
	void transformWeights(GMatrix& transform, const GVec& offset);

	/// Adjusts weights such that values in the new range will result in the
	/// same behavior that previously resulted from values in the old range.
	virtual void renormalizeInput(size_t input, double oldMin, double oldMax, double newMin = 0.0, double newMax = 1.0);
};




/// The base class of layers that apply an activation function, such as tanh, in an element-wise manner.
class GLayerActivation : public GNeuralNetLayer
{
protected:
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.

public:
	GLayerActivation();
	GLayerActivation(GDomNode* pNode);

	/// Returns true iff this layer operates only on individual elements
	virtual bool elementWise() const { return true; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const override { return m_activation.cols(); }

	/// Returns the number of values that this layer outputs.
	virtual size_t outputs() const override { return m_activation.cols(); }

	/// Resizes this layer. If pRand is non-NULL, then it preserves existing weights when possible
	/// and initializes any others to small random values.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Resizes the inputs, which in this layer also resizes the outputs
	virtual void resizeInputs(GNeuralNetLayer *pUpStreamLayer) override
	{
		resize(pUpStreamLayer->outputs(), pUpStreamLayer->outputs());
	}

	/// Returns a buffer where the activation from the most-recent call to feedForward is stored.
	virtual GVec& activation() override { return m_activation[0]; }

	/// Returns a buffer where the error terms for each unit are stored.
	virtual GVec& error() override { return m_activation[1]; }

	/// Feeds an input vector through this layer to compute the output of this layer.
	virtual void feedForward(const GVec& in) override;

	/// Computes the activation error of the layer that feeds into this one.
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;

	virtual double eval(double x) = 0;
	virtual double derivative(double x, double f_x) = 0;
};



/// An element-wise nonlinearity layer
// (Note, the following code is a bit faster:
// 		if(std::abs(x) >= 700.0)
// 			return (x >= 0 ? 1.0 : -1.0);
// 		double a = exp(x);
// 		double b = 1.0 / a;
// 		return (a - b) / (a + b);
// and here is a fast version of the derivative
// 		if(std::abs(x) >= 700.0)
// 			return (x >= 0 ? 1.0 : 0.0);
// 		double a = exp(x);
// 		double b = 1.0 / a;
// 		double d = 2.0 / (a + b); // sech(x)
// 		return d * d;
class GLayerTanh : public GLayerActivation
{
public:
	GLayerTanh() : GLayerActivation() {}
	GLayerTanh(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_tanh; }
	virtual double eval(double x) override { return std::tanh(x); }
	virtual double derivative(double x, double f_x) override { return 1.0 - (f_x * f_x); }
};



/// An element-wise nonlinearity layer
class GLayerLogistic : public GLayerActivation
{
public:
	GLayerLogistic() : GLayerActivation() {}
	GLayerLogistic(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_logistic; }
	virtual double eval(double x) override
	{
		if(x >= 700.0) // Don't trigger a floating point exception
			return 1.0;
		else if(x < -700.0) // Don't trigger a floating point exception
			return 0.0;
		else return 1.0 / (std::exp(-x) + 1.0);
		
	}
	virtual double derivative(double x, double f_x) override { return f_x * (1.0 - f_x); }
};



#define BEND_AMOUNT 0.5
#define BEND_SIZE 0.5
/// An element-wise nonlinearity layer
class GLayerBentIdentity : public GLayerActivation
{
public:
	GLayerBentIdentity() : GLayerActivation() {}
	GLayerBentIdentity(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_bentidentity; }
	virtual double eval(double x) override { return BEND_AMOUNT * (std::sqrt(x * x + BEND_SIZE * BEND_SIZE) - BEND_SIZE) + x; }
	virtual double derivative(double x, double f_x) override { return BEND_AMOUNT * x / std::sqrt(x * x + BEND_SIZE * BEND_SIZE) + 1.0; }
};



/// An element-wise nonlinearity layer
/// This activation function forms a sigmoid shape by splicing exponential and logarithmic functions together.
class GLayerSigExp : public GLayerActivation
{
public:
	GLayerSigExp() : GLayerActivation() {}
	GLayerSigExp(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_sigexp; }
	virtual double eval(double x) override { return (x <= 0.0 ? exp(x) - 1.0 : std::log(x + 1.0)); }
	virtual double derivative(double x, double f_x) override { return (x <= 0.0 ? std::exp(x) : 1.0 / (x + 1.0)); }
};



/// An element-wise nonlinearity layer
class GLayerGaussian : public GLayerActivation
{
public:
	GLayerGaussian() : GLayerActivation() {}
	GLayerGaussian(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_gaussian; }
	virtual double eval(double x) override { return std::exp(-(x * x)); }
	virtual double derivative(double x, double f_x) override { return -2.0 * x * std::exp(-(x * x)); }
};



/// An element-wise nonlinearity layer
class GLayerSine : public GLayerActivation
{
public:
	GLayerSine() : GLayerActivation() {}
	GLayerSine(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_sine; }
	virtual double eval(double x) override { return std::sin(x); }
	virtual double derivative(double x, double f_x) override { return std::cos(x); }
};




/// An element-wise nonlinearity layer
class GLayerRectifier : public GLayerActivation
{
public:
	GLayerRectifier() : GLayerActivation() {}
	GLayerRectifier(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_rectifier; }
	virtual double eval(double x) override { return std::max(0.0, x); }
	virtual double derivative(double x, double f_x) override { return (x >= 0.0 ? 1.0 : 0.0); }
};




/// An element-wise nonlinearity layer
class GLayerLeakyRectifier : public GLayerActivation
{
public:
	GLayerLeakyRectifier() : GLayerActivation() {}
	GLayerLeakyRectifier(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_leakyrectifier; }
	virtual double eval(double x) override { return x >= 0.0 ? x : 0.01 * x; }
	virtual double derivative(double x, double f_x) override { return x >= 0.0 ? 1.0 : 0.01; }
};



/// An element-wise nonlinearity layer
// (Note: A similar, but less well-known function is the integral of the logistic function. I think it is slightly faster to compute.)
class GLayerSoftPlus : public GLayerActivation
{
public:
	GLayerSoftPlus() : GLayerActivation() {}
	GLayerSoftPlus(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_softplus; }
	virtual double eval(double x) override { return x > 500 ? x : log(1.0 + exp(x)); }
	virtual double derivative(double x, double f_x) override { return 1.0 / (1.0 + exp(-x)); }
};






/// An element-wise nonlinearity layer.
/// This function is shaped like a sigmoid, but with both a co-domain and domain
/// that spans the continuous values. At very negative values,
/// it is shaped like y=-sqrt(-2x). Near zero, it is shaped
/// like y=x. At very positive values, it is shaped like y=sqrt(2x).
class GLayerSoftRoot : public GLayerActivation
{
public:
	GLayerSoftRoot() : GLayerActivation() {}
	GLayerSoftRoot(GDomNode* pNode) : GLayerActivation(pNode) {}
	virtual LayerType type() const override { return layer_softroot; }
	virtual double eval(double x) override
	{
		double d = std::sqrt(x * x + 1.0);
		return std::sqrt(d + x) - std::sqrt(d - x);
	}
	virtual double derivative(double x, double f_x) override
	{
		if(std::abs(x) > 1e7)
			return 0.0;
		double d = std::sqrt(x * x + 1.0);
		double t = x / d;
		return (t + 1.0) / (2.0 * std::sqrt(d + x)) - (t - 1.0) / (2.0 * std::sqrt(d - x));
	}
};



/// Multiplies each pair of values together to produce the output
class GLayerProductPooling : public GNeuralNetLayer
{
friend class GNeuralNet;
protected:
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.

public:
using GNeuralNetLayer::feedForward;

	/// General-purpose constructor.
	GLayerProductPooling(size_t inputs = FLEXIBLE_SIZE);

	/// Deserializing constructor
	GLayerProductPooling(GDomNode* pNode);
	~GLayerProductPooling();

	/// Returns the type of this layer
	virtual LayerType type() const override { return layer_productpooling; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const override { return m_activation.cols() * 2; }

	/// Returns the number of outputs that this layer produces.
	virtual size_t outputs() const override { return m_activation.cols(); }

	/// Resizes this layer. outputs must be 2*inputs.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Resizes the inputs of this layer (as in the above function) given the upstream layer to calculate needed inputs.
	virtual void resizeInputs(GNeuralNetLayer* pUpStreamLayer) override
	{
		resize(pUpStreamLayer->outputs(), pUpStreamLayer->outputs() / 2);
	}

	/// Returns the activation values from the most recent call to feedForward().
	virtual GVec& activation() override { return m_activation[0]; }

	/// Returns a buffer used to store error terms for each unit in this layer.
	virtual GVec& error() override { return m_activation[1]; }

	/// Feeds a the inputs through this layer.
	virtual void feedForward(const GVec& in) override;

	/// Backpropagates the error from this layer into the upstream layer's error vector.
	/// (Assumes that the error in this layer has already been computed and deactivated.
	/// The error this computes is with respect to the output of the upstream layer.)
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;
};


/// Adds each pair of values together to produce the output
class GLayerAdditionPooling : public GNeuralNetLayer
{
friend class GNeuralNet;
protected:
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.

public:
using GNeuralNetLayer::feedForward;

	/// General-purpose constructor.
	GLayerAdditionPooling(size_t inputs = FLEXIBLE_SIZE);

	/// Deserializing constructor
	GLayerAdditionPooling(GDomNode* pNode);
	~GLayerAdditionPooling();

	/// Returns the type of this layer
	virtual LayerType type() const override { return layer_additionpooling; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const override { return m_activation.cols() * 2; }

	/// Returns the number of outputs that this layer produces.
	virtual size_t outputs() const override { return m_activation.cols(); }

	/// Resizes this layer. outputs must be 2*inputs.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Resizes the inputs of this layer (as in the above function) given the upstream layer to calculate needed inputs.
	virtual void resizeInputs(GNeuralNetLayer* pUpStreamLayer) override
	{
		resize(pUpStreamLayer->outputs(), pUpStreamLayer->outputs() / 2);
	}

	/// Returns the activation values from the most recent call to feedForward().
	virtual GVec& activation() override { return m_activation[0]; }

	/// Returns a buffer used to store error terms for each unit in this layer.
	virtual GVec& error() override { return m_activation[1]; }

	/// Feeds a the inputs through this layer.
	virtual void feedForward(const GVec& in) override;

	/// Backpropagates the error from this layer into the upstream layer's error vector.
	/// (Assumes that the error in this layer has already been computed and deactivated.
	/// The error this computes is with respect to the output of the upstream layer.)
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;
};


class GLayerMaxOut : public GParameterizedLayer
{
friend class GNeuralNet;
protected:
	GMatrix m_weights; // Each row is an upstream neuron. Each column is a downstream neuron.
	GMatrix m_bias; // Row 0 is the bias. Row 1 is the bias delta.
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.
	GIndexVec m_winners; // The indexes of the winning inputs.

public:
using GNeuralNetLayer::feedForward;

	/// General-purpose constructor. Takes ownership of pActivationFunction.
	/// If pActivationFunction is NULL, then GActivationTanH is used.
	GLayerMaxOut(size_t inputs, size_t outputs);

	/// Deserializing constructor
	GLayerMaxOut(GDomNode* pNode);
	~GLayerMaxOut();

	/// Returns the type of this layer
	virtual LayerType type() const override { return layer_maxout; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const override { return m_weights.rows(); }

	/// Returns the number of nodes or units in this layer.
	virtual size_t outputs() const override { return m_weights.cols(); }

	/// Resizes this layer. If pRand is non-NULL, then it preserves existing weights when possible
	/// and initializes any others to small random values.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Returns the activation values from the most recent call to feedForward().
	virtual GVec& activation() override { return m_activation[0]; }

	/// Returns a buffer used to store error terms for each unit in this layer.
	virtual GVec& error() override { return m_activation[1]; }

	/// Feeds a the inputs through this layer.
	virtual void feedForward(const GVec& in) override;

	/// \deprecated Randomly sets the activation of some units to 0.
	virtual void dropOut(GRand& rand, double probOfDrop) override;

	/// Backpropagates the error from this layer into the upstream layer's error vector.
	/// (Assumes that the error in this layer has already been computed and deactivated.
	/// The error this computes is with respect to the output of the upstream layer.)
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Updates the deltas for updating the weights by gradient descent.
	/// (Assumes the error has already been computed and deactivated.)
	virtual void updateDeltas(const GVec& upStreamActivation, GVec &deltas) override;

	/// Add the weight and bias deltas to the weights.
	virtual void applyDeltas(double learningRate, const GVec &deltas) override;

	/// Multiplies all the weights in this layer by the specified factor.
	virtual void scaleWeights(double factor, bool scaleBiases) override;

	/// Diminishes all the weights (that is, moves them in the direction toward 0) by the specified amount.
	virtual void diminishWeights(double amount, bool regularizeBiases) override;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;

	/// Serialize the weights in this layer into a vector. Return the number of elements written.
	virtual size_t weightsToVector(double* pOutVector) override;

	/// Deserialize from a vector to the weights in this layer. Return the number of elements consumed.
	virtual size_t vectorToWeights(const double* pVector) override;

	/// Copy the weights from pSource to this layer. (Assumes pSource is the same type of layer.)
	virtual void copyWeights(const GNeuralNetLayer* pSource) override;

	/// Initialize the weights with small random values.
	virtual void resetWeights(GRand& rand) override;

	/// Perturbs the weights that feed into the specifed units with Gaussian noise.
	/// start specifies the first unit whose incoming weights are perturbed.
	/// count specifies the maximum number of units whose incoming weights are perturbed.
	/// The default values for these parameters apply the perturbation to all units.
	virtual void perturbWeights(GRand& rand, double deviation, size_t start = 0, size_t count = INVALID_INDEX) override;

	/// Scales weights if necessary such that the manitude of the weights (not including the bias) feeding into each unit are <= max.
	virtual void maxNorm(double min, double max) override;

	/// Returns a reference to the weights matrix of this layer
	GMatrix& weights() { return m_weights; }

	/// Returns a reference to the weights matrix of this layer
	const GMatrix& weights() const { return m_weights; }

	/// Returns the bias vector of this layer.
	GVec& bias() { return m_bias[0]; }

	/// Returns the bias vector of this layer.
	const GVec& bias() const { return m_bias[0]; }

	/// Sets the weights of this layer to make it weakly approximate the identity function.
	/// start specifies the first unit whose incoming weights will be adjusted.
	/// count specifies the maximum number of units whose incoming weights are adjusted.
	void setWeightsToIdentity(size_t start = 0, size_t count = (size_t)-1);

	/// Transforms the weights of this layer by the specified transformation matrix and offset vector.
	/// transform should be the pseudoinverse of the transform applied to the inputs. pOffset should
	/// be the negation of the offset added to the inputs after the transform, or the transformed offset
	/// that is added before the transform.
	void transformWeights(GMatrix& transform, const GVec& offset);

	void copySingleNeuronWeights(size_t source, size_t dest);
};






class GLayerRestrictedBoltzmannMachine : public GParameterizedLayer
{
protected:
	GMatrix m_weights; // Each column is an upstream neuron. Each row is a downstream neuron.
	GMatrix m_bias; // Row 0 is the bias. Row 1 is the activation. Row 2 is the error.
	GMatrix m_biasReverse; // Row 0 is the bias. Row 1 is the activation. Row 2 is the error.

public:
using GNeuralNetLayer::feedForward;

	/// General-purpose constructor. Takes ownership of pActivationFunction.
	GLayerRestrictedBoltzmannMachine(size_t inputs, size_t outputs);

	/// Deserializing constructor
	GLayerRestrictedBoltzmannMachine(GDomNode* pNode);

	/// Returns the type of this layer
	virtual LayerType type() const override { return layer_restrictedboltzmannmachine; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of visible units.
	virtual size_t inputs() const override { return m_weights.cols(); }

	/// Returns the number of hidden units.
	virtual size_t outputs() const override { return m_weights.rows(); }

	/// Resizes this layer. If pRand is non-NULL, then it preserves existing weights when possible
	/// and initializes any others to small random values.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Returns the activation values on the hidden end.
	virtual GVec& activation() override { return m_bias[1]; }

	/// Returns a buffer used to store error terms for each unit in this layer.
	virtual GVec& error() override { return m_bias[2]; }

	/// Feeds pIn forward through this layer.
	virtual void feedForward(const GVec& in) override;

	/// \deprecated Randomly sets the activation of some units to 0.
	virtual void dropOut(GRand& rand, double probOfDrop) override;

	/// Feed a vector from the hidden end to the visible end. The results are placed in activationReverse();
	void feedBackward(const GVec& in);

	/// Backpropagates the error from this layer into the upstream layer's error vector.
	/// (Assumes that the error in this layer has already been deactivated.
	/// The error this computes is with respect to the output of the upstream layer.)
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Updates the deltas for updating the weights by gradient descent.
	/// (Assumes the error has already been computed and deactivated.)
	virtual void updateDeltas(const GVec &upStreamActivation, GVec &deltas) override;

	/// Add the weight and bias deltas to the weights.
	virtual void applyDeltas(double learningRate, const GVec &deltas) override;

	/// Multiplies all the weights in this layer by the specified factor.
	virtual void scaleWeights(double factor, bool scaleBiases) override;

	/// Diminishes all the weights (that is, moves them in the direction toward 0) by the specified amount.
	virtual void diminishWeights(double amount, bool regularizeBiases) override;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;

	/// Serialize the weights in this layer into a vector. Return the number of elements written.
	virtual size_t weightsToVector(double* pOutVector) override;

	/// Deserialize from a vector to the weights in this layer. Return the number of elements consumed.
	virtual size_t vectorToWeights(const double* pVector) override;

	/// Copy the weights from pSource to this layer. (Assumes pSource is the same type of layer.)
	virtual void copyWeights(const GNeuralNetLayer* pSource) override;

	/// Initialize the weights with small random values.
	virtual void resetWeights(GRand& rand) override;

	/// Perturbs the weights that feed into the specifed units with Gaussian noise.
	/// Also perturbs the bias.
	/// start specifies the first unit whose incoming weights are perturbed.
	/// count specifies the maximum number of units whose incoming weights are perturbed.
	/// The default values for these parameters apply the perturbation to all units.
	virtual void perturbWeights(GRand& rand, double deviation, size_t start = 0, size_t count = INVALID_INDEX) override;

	/// Scales weights if necessary such that the manitude of the weights (not including the bias) feeding into each unit are <= max.
	virtual void maxNorm(double min, double max) override;

	/// Returns a reference to the weights matrix of this layer
	GMatrix& weights() { return m_weights; }

	/// Returns the bias for the hidden end of this layer.
	GVec& bias() { return m_bias[0]; }

	/// Returns the bias for the hidden end of this layer.
	const GVec& bias() const { return m_bias[0]; }

	/// Returns the bias for the visible end of this layer.
	GVec& biasReverse() { return m_biasReverse[0]; }

	/// Returns the activation for the visible end of this layer.
	GVec& activationReverse() { return m_biasReverse[1]; }

	/// Returns the error term for the visible end of this layer.
	GVec& errorReverse() { return m_biasReverse[2]; }

	/// Performs binomial resampling of the activation values on the output end of this layer.
	void resampleHidden(GRand& rand);

	/// Performs binomial resampling of the activation values on the input end of this layer.
	void resampleVisible(GRand& rand);

	/// Draws a sample observation from "iters" iterations of Gibbs sampling.
	/// The resulting sample is placed in activationReverse(), and the corresponding
	/// encoding will be in activation().
	void drawSample(GRand& rand, size_t iters);

	/// Returns the free energy of this layer.
	double freeEnergy(const GVec& visibleSample);

	/// Refines this layer by contrastive divergence.
	/// pVisibleSample should point to a vector of inputs that will be presented to this layer.
	void contrastiveDivergence(GRand& rand, const GVec& visibleSample, double learningRate, size_t gibbsSamples = 1);
};



class GLayerConvolutional1D : public GParameterizedLayer
{
protected:
	size_t m_inputSamples;
	size_t m_inputChannels;
	size_t m_outputSamples;
	size_t m_kernelsPerChannel;
	GMatrix m_kernels;
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.
	GVec m_bias;

public:
using GNeuralNetLayer::feedForward;

	/// General-purpose constructor.
	/// For example, if you collect 19 samples from 3 sensors, then the total input size will be 57 (19*3=57).
	/// The three values collected at time 0 will come first, followed by the three values collected at
	/// time 1, and so forth. If kernelSize is 5, then the output will consist of 15 (19-5+1=15) samples.
	/// If kernelsPerChannel is 2, then there will be 6 (3*2=6) channels in the output, for a total of 90 (15*6=90)
	/// output values. The first six channel values will appear first in the output vector, followed by the next six,
	/// and so forth. (kernelSize must be <= inputSamples.)
	GLayerConvolutional1D(size_t inputSamples, size_t inputChannels, size_t kernelSize, size_t kernelsPerChannel);

	/// Deserializing constructor
	GLayerConvolutional1D(GDomNode* pNode);

	/// Returns the type of this layer
	virtual LayerType type() const override { return layer_convolutional1d; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const override { return m_inputSamples * m_inputChannels; }

	/// Returns the number of nodes or units in this layer.
	virtual size_t outputs() const override { return m_outputSamples * m_inputChannels * m_kernelsPerChannel; }

	/// Resizes this layer. If pRand is non-NULL, an exception is thrown.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Returns the activation values from the most recent call to feedForward().
	virtual GVec& activation() override { return m_activation[0]; }

	/// Returns a buffer used to store error terms for each unit in this layer.
	virtual GVec& error() override { return m_activation[1]; }

	/// Feeds a the inputs through this layer.
	virtual void feedForward(const GVec& in) override;

	/// \deprecated Randomly sets the activation of some units to 0.
	virtual void dropOut(GRand& rand, double probOfDrop) override;

	/// Throws an exception, because convolutional layers do not support dropConnect.
	virtual void dropConnect(GRand& rand, double probOfDrop);

	/// Backpropagates the error from this layer into the upstream layer's error vector.
	/// (Assumes that the error in this layer has already been computed and deactivated.
	/// The error this computes is with respect to the output of the upstream layer.)
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Updates the deltas for updating the weights by gradient descent.
	/// (Assumes the error has already been computed and deactivated.)
	virtual void updateDeltas(const GVec &upStreamActivation, GVec &deltas) override;

	/// Add the weight and bias deltas to the weights.
	virtual void applyDeltas(double learningRate, const GVec &deltas) override;

	/// Multiplies all the weights in this layer by the specified factor.
	virtual void scaleWeights(double factor, bool scaleBiases) override;

	/// Diminishes all the weights (that is, moves them in the direction toward 0) by the specified amount.
	virtual void diminishWeights(double amount, bool regularizeBiases) override;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;

	/// Serialize the weights in this layer into a vector. Return the number of elements written.
	virtual size_t weightsToVector(double* pOutVector) override;

	/// Deserialize from a vector to the weights in this layer. Return the number of elements consumed.
	virtual size_t vectorToWeights(const double* pVector) override;

	/// Copy the weights from pSource to this layer. (Assumes pSource is the same type of layer.)
	virtual void copyWeights(const GNeuralNetLayer* pSource) override;

	/// Initialize the weights with small random values.
	virtual void resetWeights(GRand& rand) override;

	/// Perturbs the weights that feed into the specifed units with Gaussian noise.
	/// start specifies the first unit whose incoming weights are perturbed.
	/// count specifies the maximum number of units whose incoming weights are perturbed.
	virtual void perturbWeights(GRand& rand, double deviation, size_t start, size_t count) override;

	/// Clips each kernel weight (not including the bias) to fall between -max and max.
	virtual void maxNorm(double min, double max) override;

	const GVec& bias() const { return m_bias; }
	GVec& bias() { return m_bias; }
	const GMatrix& kernels() const { return m_kernels; }
	GMatrix& kernels() { return m_kernels; }
};




class GLayerConvolutional2D : public GParameterizedLayer
{
protected:
	/// Image abstraction to facilitate convolution
	struct Image
	{
		static size_t npos;

		Image(GVec *data, size_t width, size_t height, size_t channels);
		size_t index(size_t x, size_t y, size_t z) const;
		double read(size_t x, size_t y, size_t z = 0) const;
		double &at(size_t x, size_t y, size_t z = 0);

		/// image data
		GVec *data;
		size_t width, height, channels;
		bool interlaced;

		/// viewport data
		mutable size_t dx, dy, dz;	///< offset
		mutable size_t px, py;		///< padding
		mutable size_t sx, sy;		///< stride
		mutable bool invertStride;	///< whether the stride should be inverted (i.e. sx or sy zeroes between each value)
		mutable bool flip;			///< whether to "flip" the image (i.e. 180 degree rotation)
	};

	/// Input dimensions
	size_t m_width, m_height, m_channels;

	/// Kernel dimensions (kernel channels = input channels)
	size_t m_kWidth, m_kHeight;

	/// Output dimensions (derived; output channels = kernel count)
	size_t m_outputWidth, m_outputHeight;

	/// Data
	GVec m_bias, m_biasDelta;
	GMatrix m_kernels;
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.

	/// Data as images
	Image m_kernelImage, m_deltaImage;
	Image m_inputImage, m_upStreamErrorImage;
	Image m_actImage, m_errImage;

private:
	/// Helper functions for convolution
	double filterSum(const Image &in, const Image &filter, size_t channels);
	void addScaled(const Image &in, double scalar, Image &out);
	void convolve(const Image &in, const Image &filter, Image &out, size_t channels = none);
	void convolveFull(const Image &in, const Image &filter, Image &out, size_t channels = none);
	void updateOutputSize();

public:
	static size_t none;

	/// General-purpose constructor.
	GLayerConvolutional2D(size_t width, size_t height, size_t channels, size_t kWidth, size_t kHeight, size_t kCount = 0);

	/// Constructor that will automatically use the upstream convolutional layer when added to a neural network
	GLayerConvolutional2D(size_t kWidth, size_t kHeight, size_t kCount = 0);

	GLayerConvolutional2D(GDomNode* pNode);

	virtual LayerType type() const override { return layer_convolutional2d; }
	virtual GDomNode *serialize(GDom *pDoc) override;
	virtual std::string to_str() override;
	virtual size_t inputs() const override { return m_width * m_height * m_channels; }
	virtual size_t outputs() const override { return m_outputWidth * m_outputHeight * m_bias.size(); }
	virtual void resize(size_t inputs, size_t outputs) override;
	virtual void resizeInputs(GNeuralNetLayer *pUpStreamLayer) override;
	virtual GVec &activation() override { return m_activation[0]; }
	virtual GVec &error() override { return m_activation[1]; }

	virtual void feedForward(const GVec &in) override;

	/// \deprecated
	virtual void dropOut(GRand &rand, double probOfDrop) override;
	virtual void dropConnect(GRand &rand, double probOfDrop);
	virtual void backPropError(GNeuralNetLayer *pUpStreamLayer) override;

	/// Updates the deltas for updating the weights by gradient descent.
	/// (Assumes the error has already been computed and deactivated.)
	virtual void updateDeltas(const GVec &upStreamActivation, GVec &deltas) override;

	/// Add the weight and bias deltas to the weights.
	virtual void applyDeltas(double learningRate, const GVec &deltas) override;

	virtual void scaleWeights(double factor, bool scaleBiases) override;
	virtual void diminishWeights(double amount, bool regularizeBiases) override;
	virtual size_t countWeights() override;
	virtual size_t weightsToVector(double *pOutVector) override;
	virtual size_t vectorToWeights(const double *pVector) override;
	virtual void copyWeights(const GNeuralNetLayer *pSource) override;
	virtual void resetWeights(GRand &rand) override;
	virtual void perturbWeights(GRand &rand, double deviation, size_t start = 0, size_t count = INVALID_INDEX) override;
	virtual void maxNorm(double min, double max) override;

	void setPadding(size_t px, size_t py = none);
	void setStride(size_t sx, size_t sy = none);
	void setInterlaced(bool interlaced);
	void setInputInterlaced(bool interlaced);
	void setKernelsInterlaced(bool interlaced);
	void setOutputInterlaced(bool interlaced);
	void addKernel();
	void addKernels(size_t n);

	size_t inputWidth() const { return m_width; }
	size_t inputHeight() const { return m_height; }
	size_t inputChannels() const { return m_channels; }

	size_t kernelWidth() const { return m_kWidth; }
	size_t kernelHeight() const { return m_kHeight; }
	size_t kernelChannels() const { return m_channels; }

	size_t outputWidth() const { return m_outputWidth; }
	size_t outputHeight() const { return m_outputHeight; }
	size_t outputChannels() const { return m_bias.size(); }

	size_t kernelCount() const { return m_kernels.rows(); }
	const GMatrix &kernels() const { return m_kernels; }
	GMatrix &kernels() { return m_kernels; }
	const GVec &bias() const { return m_bias; }
	GVec &bias() { return m_bias; }
};



class GMaxPooling2D : public GNeuralNetLayer
{
protected:
	size_t m_inputCols;
	size_t m_inputRows;
	size_t m_inputChannels;
	size_t m_regionSize;
	GMatrix m_activation; // Row 0 is the activation. Row 1 is the error.

public:
using GNeuralNetLayer::feedForward;

	/// General-purpose constructor.
	/// For example, if your input is a 64x48 color (RGB) image, then inputCols will be 64, inputRows will be 48,
	/// and inputChannels will be 3. The total input size will be 9216 (64*48*3=9216).
	/// The values should be presented in the following order: c1x1y1, c2x1y1, c1x2y1, c2x2y1, c1x1y2, ...
	/// If kernelSize is 5, then the output will consist of 60 columns (64-5+1=60) and 44 rows (48-5+1=44).
	/// If kernelsPerChannel is 2, then there will be 6 (3*2=6) channels in the output, for a total of
	/// 15840 (60*44*6=15840) output values. (kernelSize must be <= inputSamples.)
	GMaxPooling2D(size_t inputCols, size_t inputRows, size_t inputChannels, size_t regionSize = 2);

	/// Deserializing constructor
	GMaxPooling2D(GDomNode* pNode);

	virtual ~GMaxPooling2D();

	/// Returns the type of this layer
	virtual LayerType type() const override { return layer_maxpooling; }

	/// Marshall this layer into a DOM.
	virtual GDomNode* serialize(GDom* pDoc) override;

	/// Makes a string representation of this layer
	virtual std::string to_str() override;

	/// Returns the number of values expected to be fed as input into this layer.
	virtual size_t inputs() const override { return m_inputRows * m_inputCols * m_inputChannels; }

	/// Returns the number of nodes or units in this layer.
	virtual size_t outputs() const override { return m_inputRows * m_inputCols * m_inputChannels / (m_regionSize * m_regionSize); }

	/// Resizes this layer. If pRand is non-NULL, an exception is thrown.
	virtual void resize(size_t inputs, size_t outputs) override;

	/// Returns the activation values from the most recent call to feedForward().
	virtual GVec& activation() override { return m_activation[0]; }

	/// Returns a buffer used to store error terms for each unit in this layer.
	virtual GVec& error() override { return m_activation[1]; }

	/// Feeds a the inputs through this layer.
	virtual void feedForward(const GVec& in) override;

	/// Backpropagates the error from this layer into the upstream layer's error vector.
	/// (Assumes that the error in this layer has already been computed and deactivated.
	/// The error this computes is with respect to the output of the upstream layer.)
	virtual void backPropError(GNeuralNetLayer* pUpStreamLayer) override;

	/// Returns the number of double-precision elements necessary to serialize the weights of this layer into a vector.
	virtual size_t countWeights() override;
};



} // namespace GClasses

#endif // __GLAYER_H__
