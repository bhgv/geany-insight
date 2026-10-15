namespace GlobalType {
struct Value {};
int counter;
void ping();
}
::GlobalType::Value global_value;
::GlobalType::Value *global_pointer;
namespace Local {
struct Value {};
::GlobalType::Value absolute_value;
void use() {
    ::GlobalType::Value local_value;
    ::GlobalType::Value *local_pointer;
    ::GlobalType::ping();
    ::GlobalType::counter = 1;
}
}
