#include "parameter_block.h"

#include <algorithm>

namespace chi_data_types
{
//#################################################################
std::string ParameterBlockTypeName(ParameterBlockType type)
{
  switch (type)
  {
    case ParameterBlockType::BOOLEAN: return "BOOLEAN";
    case ParameterBlockType::FLOAT:   return "FLOAT";
    case ParameterBlockType::STRING:  return "STRING";
    case ParameterBlockType::INTEGER: return "INTEGER";
    case ParameterBlockType::ARRAY:   return "ARRAY";
    case ParameterBlockType::BLOCK:   return "BLOCK";
    default:
      throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
      ": No name associated with type");
  }
}

//#################################################################
ParameterBlock::ParameterBlock(const std::string& name) :
  type_(ParameterBlockType::BLOCK),
  name_(name)
{}

//Accessors
ParameterBlockType ParameterBlock::Type() const {return type_;}
std::string ParameterBlock::Name() const {return name_;}

//#################################################################
const Varying& ParameterBlock::Value() const
{
  switch (this->Type())
  {
    case ParameterBlockType::BOOLEAN:
    case ParameterBlockType::FLOAT:
    case ParameterBlockType::STRING:
    case ParameterBlockType::INTEGER:
    {
      if (value_ptr_ == nullptr)
        throw std::runtime_error(std::string(__PRETTY_FUNCTION__) +
          ": Uninitialized Varying value for block " + this->Name());
      return *value_ptr_;
    }
    default:
      throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
      ": Called for block of type " + ParameterBlockTypeName(this->Type()) +
      " which has no value.");
  }
}

//#################################################################
/**Returns the number of parameters in a block. This is normally
 * only useful for the ARRAY type.*/
size_t ParameterBlock::NumParameters() const
{
  return parameters_.size();
}

//Mutators
//#################################################################
/**Changes the block type to array, making it accessible via integer
 * keys.*/
void ParameterBlock::ChangeToArray()
{
  const std::string fname = __PRETTY_FUNCTION__;
  if (parameters_.empty())
  {
    type_ = ParameterBlockType::ARRAY;
    return;
  }

  const auto& first_param = parameters_.front();
  for (const auto& param : parameters_)
    if (param->Type() != first_param->Type())
      throw std::logic_error(fname + ": Cannot change ParameterBlock to "
        "array. It has existing parameters and they are not of the same"
        "type.");
}

//#################################################################
/**Adds a parameter to the sub-parameter list.*/
void ParameterBlock::AddParameter(ParameterBlockPtr block)
{
  for (const auto& param : parameters_)
    if (param->Name() == block->Name())
      throw std::invalid_argument(std::string(__PRETTY_FUNCTION__) +
      ": Attempting to add duplicate parameter " + param->Name() + " to "
      "block " + this->Name());
  parameters_.push_back(std::move(block));

  SortParameters();
}

//#################################################################
/**Sorts the sub-parameter list according to name. This is useful
* for regression testing.*/
void ParameterBlock::SortParameters()
{
  struct AlphabeticFunctor
  {
    bool operator()(const ParameterBlockPtr& paramA,
                    const ParameterBlockPtr& paramB)
    {
      return paramA->Name() < paramB->Name();
    }
  };

  std::sort(parameters_.begin(), parameters_.end(), AlphabeticFunctor());
}

//#################################################################
/**Returns true if a parameter with the specified name is in the
 * list of sub-parameters. Otherwise, false.*/
bool ParameterBlock::Has(const std::string &param_name) const
{
  for (const auto& param : parameters_)
    if (param->Name() == param_name)
      return true;

  return false;
}

//#################################################################
/**Gets a parameter by name.*/
const ParameterBlock& ParameterBlock::
  GetParam(const std::string &param_name) const
{
  for (const auto& param : parameters_)
    if (param->Name() == param_name)
      return *param;

  throw std::out_of_range(std::string(__PRETTY_FUNCTION__) +
  ": Parameter " + param_name + " not present in block");
}

//#################################################################
/**Gets a parameter by index.*/
const ParameterBlock& ParameterBlock::GetParam(size_t index) const
{
  try
  {
    return *parameters_.at(index);
  }
  catch (const std::out_of_range& oor)
  {
    throw std::out_of_range(std::string(__PRETTY_FUNCTION__) +
      ": Parameter with index " + std::to_string(index) +
      " not present in block");
  }
}


//#################################################################
// NOLINTBEGIN(misc-no-recursion)
/**Print the block tree structure into a designated string.*/
void ParameterBlock::RecursiveDumpToString(std::string& outstr,
                                           const std::string& offset) const
{
  outstr += offset + this->Name() + " = \n";
  outstr += offset + "{\n";

  for (const auto& param : parameters_)
  {

    switch (param->Type())
    {
      case ParameterBlockType::BOOLEAN:
      {
        outstr += offset + "  " + param->Name() + " = ";
        const bool value = param->Value().BoolValue();
        outstr += std::string( value ? "true" : "false") + ",\n";
        break;
      }
      case ParameterBlockType::FLOAT:
      {
        outstr += offset + "  " + param->Name() + " = ";
        const double value = param->Value().FloatValue();
        outstr += std::to_string(value) + ",\n";
        break;
      }
      case ParameterBlockType::STRING:
      {
        outstr += offset + "  " + param->Name() + " = ";
        const auto& value = param->Value().StringValue();
        outstr += "\"" + value + "\",\n";
        break;
      }
      case ParameterBlockType::INTEGER:
      {
        outstr += offset + "  " + param->Name() + " = ";
        const int64_t value = param->Value().IntegerValue();
        outstr += std::to_string(value) + ",\n";
        break;
      }
      case ParameterBlockType::ARRAY:
      case ParameterBlockType::BLOCK:
      {
        param->RecursiveDumpToString(outstr, offset + "  ");
        break;
      }
      default: break;
    }
  }//for parameter

  outstr += offset + "}\n";
}
// NOLINTEND(misc-no-recursion)

}//namespace chi_data_types