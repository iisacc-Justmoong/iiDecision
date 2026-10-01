#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace iiDecision::detail::onnx {
// Small, write-only encoder for the fixed graph below, using the official
// ONNX ModelProto field numbers. No general graph builder or protobuf dependency.
struct Message {
    std::string bytes;
    void varint(std::uint64_t value) {
        while (value >= 128) { bytes.push_back(static_cast<char>((value & 127) | 128)); value >>= 7; }
        bytes.push_back(static_cast<char>(value));
    }
    void integer(unsigned field, std::uint64_t value) { varint(field * 8); varint(value); }
    void string(unsigned field,std::string_view value) {
        varint(field * 8 + 2); varint(value.size()); bytes.append(value);
    }
    void message(unsigned field,const Message& value) { string(field,value.bytes); }
};
inline Message valueInfo(std::string_view name,std::uint64_t rows,std::uint64_t columns) {
    Message shape;
    for (const auto size : {rows,columns}) { Message dimension; dimension.integer(1,size); shape.message(1,dimension); }
    Message tensor; tensor.integer(1,1); tensor.message(2,shape); // FLOAT + shape
    Message type; type.message(1,tensor);
    Message value; value.string(1,name); value.message(2,type); return value;
}
inline Message node(std::string_view name,std::string_view op,
    std::initializer_list<std::string_view> inputs,std::string_view output) {
    Message result;
    for (const auto input : inputs) result.string(1,input);
    result.string(2,output); result.string(3,name); result.string(4,op); return result;
}
inline std::string evidenceModel(std::uint64_t rows,std::uint64_t columns) {
    Message graph; graph.string(2,"iiDecisionEvidence");
    graph.message(1,node("difference","Sub",{"values","query"},"delta"));
    // delta is -1, 0 or 1. Squaring avoids Abs, which some native EPs do not support.
    graph.message(1,node("square","Mul",{"delta","delta"},"squared"));
    auto reduce = node("mismatch","ReduceMax",{"squared"},"mismatch");
    Message axes; axes.string(1,"axes"); axes.integer(20,7); axes.integer(8,1);
    Message keep; keep.string(1,"keepdims"); keep.integer(20,2); keep.integer(3,1);
    reduce.message(5,axes); reduce.message(5,keep); graph.message(1,reduce);
    graph.message(1,node("match","Sub",{"one","mismatch"},"mask"));
    Message one; one.integer(1,1); one.integer(1,1); one.integer(2,1); one.string(8,"one");
    constexpr char ieee_one[]{0,0,static_cast<char>(0x80),0x3f};
    one.string(9,std::string_view(ieee_one,4)); graph.message(5,one);
    graph.message(11,valueInfo("values",rows,columns));
    graph.message(11,valueInfo("query",1,columns));
    graph.message(12,valueInfo("mask",rows,1));
    graph.message(13,valueInfo("delta",rows,columns));
    graph.message(13,valueInfo("squared",rows,columns));
    graph.message(13,valueInfo("mismatch",rows,1));
    Message opset; opset.string(1,""); opset.integer(2,13);
    Message model; model.integer(1,8); model.string(2,"iiDecision");
    model.message(7,graph); model.message(8,opset); return model.bytes;
}
}
