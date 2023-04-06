#ifndef CHITECH_PARAMETER_BLOCK_H
#define CHITECH_PARAMETER_BLOCK_H

#include "varying.h"

#include <memory>
#include <vector>
#include <string>

namespace chi_data_types
{

enum class ParameterBlockType
{
  BOOLEAN = 1     /*LUA_TBOOLEAN*/,
  FLOAT  = 3     /*LUA_TNUMBER */,
  STRING  = 4     /*LUA_TSTRING */,
  INTEGER = 5,
  ARRAY   = 98,
  BLOCK   = 99
};

std::string ParameterBlockTypeName(ParameterBlockType type);

class ParameterBlock;
typedef std::unique_ptr<ParameterBlock> ParameterBlockPtr;

/**A ParameterBlock is a conceptually simple data structure that supports
 * a hierarchy of primitive parameters. There really are just 4 member variables
 * on a ParameterBlock object, they are 1) the type (as an enum), 2) the
 * name of the block, 3) a pointer to a value (which can only be a primitive
 * type), and 4) a vector of child parameters.
 *
 * If a ParameterBlock has a primitive type, i.e., BOOLEAN, FLOAT, STRING, or
 * INTEGER, then the value_ptr will contain a pointer to the value of a
 * primitive type. Otherwise, for types ARRAY and BLOCK, the ParameterBlock
 * will not have a value_ptr and instead the vector member will contain
 * sub-parameters.*/
class ParameterBlock
{
private:
  ParameterBlockType type_ = ParameterBlockType::BLOCK;
  std::string name_;
  std::unique_ptr<Varying> value_ptr_ = nullptr;
  std::vector<ParameterBlockPtr> parameters_;

public:
  //Helpers
  template<typename T> struct IsBool
  {static constexpr bool value = std::is_same_v<T,bool>;};
  template<typename T> struct IsFloat
  {static constexpr bool value = std::is_floating_point_v<T>;};
  template<typename T> struct IsString
  {static constexpr bool value = std::is_same_v<T,std::string>;};
  template<typename T> struct IsInteger
  {static constexpr bool value = std::is_integral_v<T> and
                                 not std::is_same_v<T,bool>;};

  //Constructors
  /**Constructs an empty parameter block.*/
  explicit ParameterBlock(const std::string& name = "");

  /**Constructs one of the fundamental types.*/
  template<typename T>
  explicit ParameterBlock(const std::string& name, T value) :
    name_(name)
  {
    constexpr bool is_supported =
      IsBool<T>::value or IsFloat<T>::value or IsString<T>::value or
      IsInteger<T>::value;

    static_assert(is_supported, "Value type not supported for parameter block");

    if (IsBool<T>::value) type_ = ParameterBlockType::BOOLEAN;
    if (IsFloat<T>::value) type_ = ParameterBlockType::FLOAT;
    if (IsString<T>::value) type_ = ParameterBlockType::STRING;
    if (IsInteger<T>::value) type_ = ParameterBlockType::INTEGER;

    value_ptr_ = std::make_unique<Varying>(value);
  }

  //Accessors
  ParameterBlockType Type() const;
  std::string Name() const;
  const Varying& Value() const;
  size_t NumParameters() const;

  //Mutators
  void ChangeToArray();
public:
  //utilities
public:
  /**Adds a parameter to the sub-parameter list.*/
  void AddParameter(ParameterBlockPtr block);
  /**Makes a ParameterBlock and adds it to the sub-parameters list.*/
  template<typename T>
  void MakeAddParameter(const std::string& key_str_name, T value)
  {
    AddParameter(std::make_unique<ParameterBlock>(
      key_str_name, value));
  }
private:
  /**Sorts the sub-parameter list according to name. This is useful
   * for regression testing.*/
  void SortParameters();
public:
  /**Returns true if a parameter with the specified name is in the
   * list of sub-parameters. Otherwise, false.*/
  bool Has(const std::string& param_name) const;

  /**Gets a parameter by name.*/
  const ParameterBlock& GetParam(const std::string& param_name) const;
  /**Gets a parameter by index.*/
  const ParameterBlock& GetParam(size_t index) const;

public:
  /**Returns the value of the parameter.*/
  template<typename T>
  T GetValue() const
  {
    if (value_ptr_ == nullptr)
      throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
                             ": Value not available for block type " +
                             ParameterBlockTypeName(Type()));
    return Value().GetValue<T>();
  }

  /**Fetches the parameter with the given name and returns it value.*/
  template<typename T>
  T GetParamValue(const std::string& param_name) const
  {
    const auto& param = GetParam(param_name);
    return param.GetValue<T>();
  }

  /**Converts the parameters of an array-type parameter block to a vector of
   * primitive types and returns it.*/
  template<typename T>
  std::vector<T> GetVectorValue() const
  {
    if (Type() != ParameterBlockType::ARRAY)
      throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
                             ": Invalid type requested for parameter of type " +
                             ParameterBlockTypeName(Type()));

    std::vector<T> vec;
    if (parameters_.empty()) return vec;

    //Check the first sub-param is of the right type
    const auto& front_param = parameters_.front();

    //Check that all other parameters are of the required type
    for (const auto& param : parameters_)
      if (param->Type() != front_param->Type())
        throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
          ": Cannot construct vector from block because "
          "the sub_parameters do not all have the correct type.");

    const size_t num_params = parameters_.size();
    for (size_t k=0; k<num_params; ++k)
    {
      const auto& param = GetParam(k);
      vec.push_back(param.GetValue<T>());
    }

    return vec;
  }

  /**Gets a vector of primitive types from an array-type parameter block
   * specified as a parameter of the current block.*/
  template<typename T>
  std::vector<T> GetParamVectorValue(const std::string& param_name) const
  {
    const auto& param = GetParam(param_name);
    return param.GetVectorValue<T>();
  }

  /**Given a reference to a string, recursively travels the parameter
   * tree and print values into the reference string.*/
  void RecursiveDumpToString(std::string& outstr,
                             const std::string& offset="") const;
};

}//namespace chi_lua

#endif //CHITECH_PARAMETER_BLOCK_H
