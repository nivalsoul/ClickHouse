#pragma once

#include <DB/Dictionaries/IDictionary.h>
#include <DB/Dictionaries/IDictionarySource.h>
#include <DB/Dictionaries/DictionaryStructure.h>
#include <DB/Columns/ColumnString.h>
#include <DB/Common/Arena.h>
#include <ext/range.hpp>
#include <ext/size.hpp>
#include <atomic>
#include <vector>
#include <tuple>


namespace DB
{

class FlatDictionary final : public IDictionary
{
public:
    FlatDictionary(const std::string & name, const DictionaryStructure & dict_struct,
		DictionarySourcePtr source_ptr, const DictionaryLifetime dict_lifetime, bool require_nonempty);

	FlatDictionary(const FlatDictionary & other);

	std::exception_ptr getCreationException() const override { return creation_exception; }

	std::string getName() const override { return name; }

	std::string getTypeName() const override { return "Flat"; }

	std::size_t getBytesAllocated() const override { return bytes_allocated; }

	std::size_t getQueryCount() const override { return query_count.load(std::memory_order_relaxed); }

	double getHitRate() const override { return 1.0; }

	std::size_t getElementCount() const override { return element_count; }

	double getLoadFactor() const override { return static_cast<double>(element_count) / bucket_count; }

	bool isCached() const override { return false; }

	DictionaryPtr clone() const override { return std::make_unique<FlatDictionary>(*this); }

	const IDictionarySource * getSource() const override { return source_ptr.get(); }

	const DictionaryLifetime & getLifetime() const override { return dict_lifetime; }

	const DictionaryStructure & getStructure() const override { return dict_struct; }

	std::chrono::time_point<std::chrono::system_clock> getCreationTime() const override
	{
		return creation_time;
	}

	bool isInjective(const std::string & attribute_name) const override
	{
		return dict_struct.attributes[&getAttribute(attribute_name) - attributes.data()].injective;
	}

	bool hasHierarchy() const override { return hierarchical_attribute; }

	void toParent(const PaddedPODArray<Key> & ids, PaddedPODArray<Key> & out) const override;

#define DECLARE(TYPE)\
	void get##TYPE(const std::string & attribute_name, const PaddedPODArray<Key> & ids, PaddedPODArray<TYPE> & out) const;
	DECLARE(UInt8)
	DECLARE(UInt16)
	DECLARE(UInt32)
	DECLARE(UInt64)
	DECLARE(Int8)
	DECLARE(Int16)
	DECLARE(Int32)
	DECLARE(Int64)
	DECLARE(Float32)
	DECLARE(Float64)
#undef DECLARE

	void getString(const std::string & attribute_name, const PaddedPODArray<Key> & ids, ColumnString * out) const;

#define DECLARE(TYPE)\
	void get##TYPE(\
		const std::string & attribute_name, const PaddedPODArray<Key> & ids, const PaddedPODArray<TYPE> & def,\
		PaddedPODArray<TYPE> & out) const;
	DECLARE(UInt8)
	DECLARE(UInt16)
	DECLARE(UInt32)
	DECLARE(UInt64)
	DECLARE(Int8)
	DECLARE(Int16)
	DECLARE(Int32)
	DECLARE(Int64)
	DECLARE(Float32)
	DECLARE(Float64)
#undef DECLARE

	void getString(
		const std::string & attribute_name, const PaddedPODArray<Key> & ids, const ColumnString * const def,
		ColumnString * const out) const;

#define DECLARE(TYPE)\
	void get##TYPE(\
		const std::string & attribute_name, const PaddedPODArray<Key> & ids, const TYPE def,\
		PaddedPODArray<TYPE> & out) const;
	DECLARE(UInt8)
	DECLARE(UInt16)
	DECLARE(UInt32)
	DECLARE(UInt64)
	DECLARE(Int8)
	DECLARE(Int16)
	DECLARE(Int32)
	DECLARE(Int64)
	DECLARE(Float32)
	DECLARE(Float64)
#undef DECLARE

	void getString(
		const std::string & attribute_name, const PaddedPODArray<Key> & ids, const String & def,
		ColumnString * const out) const;;

	void has(const PaddedPODArray<Key> & ids, PaddedPODArray<UInt8> & out) const override;

private:
	template <typename Value> using ContainerType = PaddedPODArray<Value>;
	template <typename Value> using ContainerPtrType = std::unique_ptr<ContainerType<Value>>;

	struct Attribute final
	{
		AttributeUnderlyingType type;
		std::tuple<
			UInt8, UInt16, UInt32, UInt64,
			Int8, Int16, Int32, Int64,
			Float32, Float64,
			String> null_values;
		std::tuple<
			ContainerPtrType<UInt8>, ContainerPtrType<UInt16>, ContainerPtrType<UInt32>, ContainerPtrType<UInt64>,
			ContainerPtrType<Int8>, ContainerPtrType<Int16>, ContainerPtrType<Int32>, ContainerPtrType<Int64>,
			ContainerPtrType<Float32>, ContainerPtrType<Float64>,
			ContainerPtrType<StringRef>> arrays;
		std::unique_ptr<Arena> string_arena;
	};

	void createAttributes();
	void loadData();

	template <typename T>
	void addAttributeSize(const Attribute & attribute);

	void calculateBytesAllocated();

	template <typename T>
	void createAttributeImpl(Attribute & attribute, const Field & null_value);

	Attribute createAttributeWithType(const AttributeUnderlyingType type, const Field & null_value);

	template <typename OutputType, typename ValueSetter, typename DefaultGetter>
	void getItemsNumber(
		const Attribute & attribute,
		const PaddedPODArray<Key> & ids,
		ValueSetter && set_value,
		DefaultGetter && get_default) const;

	template <typename AttributeType, typename OutputType, typename ValueSetter, typename DefaultGetter>
	void getItemsImpl(
		const Attribute & attribute,
		const PaddedPODArray<Key> & ids,
		ValueSetter && set_value,
		DefaultGetter && get_default) const;

	template <typename T>
	void setAttributeValueImpl(Attribute & attribute, const Key id, const T value);

	void setAttributeValue(Attribute & attribute, const Key id, const Field & value);

	const Attribute & getAttribute(const std::string & attribute_name) const;

	template <typename T>
	void has(const Attribute & attribute, const PaddedPODArray<Key> & ids, PaddedPODArray<UInt8> & out) const;

	const std::string name;
	const DictionaryStructure dict_struct;
	const DictionarySourcePtr source_ptr;
	const DictionaryLifetime dict_lifetime;
	const bool require_nonempty;

	std::map<std::string, std::size_t> attribute_index_by_name;
	std::vector<Attribute> attributes;
	const Attribute * hierarchical_attribute = nullptr;

	std::size_t bytes_allocated = 0;
	std::size_t element_count = 0;
	std::size_t bucket_count = 0;
	mutable std::atomic<std::size_t> query_count{0};

	std::chrono::time_point<std::chrono::system_clock> creation_time;

	std::exception_ptr creation_exception;
};

}
