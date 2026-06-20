#include "binding_registrars.h"

#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cyber/message/raw_message.h"
#include "cyber/record/record_message.h"
#include "cyber/record/record_reader.h"
#include "cyber/record/record_writer.h"

namespace apollo {
namespace cyber {
namespace {

struct RecordMessageBinding {
  uint64_t timestamp = 0;
  std::string channel_name;
  std::string data;
  std::string data_type;
  bool end = true;
};

class RecordReaderBinding {
 public:
  explicit RecordReaderBinding(const std::string& file_name)
      : reader_(std::make_unique<record::RecordReader>(file_name)) {}

  void close() { reader_.reset(); }

  RecordMessageBinding read_message(
      uint64_t begin_time = 0,
      uint64_t end_time = std::numeric_limits<uint64_t>::max()) {
    EnsureOpen(reader_, "record reader");

    RecordMessageBinding result;
    record::RecordMessage record_message;
    if (!reader_->ReadMessage(&record_message, begin_time, end_time)) {
      result.end = true;
      return result;
    }

    result.end = false;
    result.channel_name = record_message.channel_name;
    result.data = record_message.content;
    result.timestamp = record_message.time;
    result.data_type = reader_->GetMessageType(record_message.channel_name);
    return result;
  }

  uint64_t get_message_number(const std::string& channel_name) {
    EnsureOpen(reader_, "record reader");
    return reader_->GetMessageNumber(channel_name);
  }

  std::string get_message_type(const std::string& channel_name) {
    EnsureOpen(reader_, "record reader");
    return reader_->GetMessageType(channel_name);
  }

  py::bytes get_proto_desc(const std::string& channel_name) {
    EnsureOpen(reader_, "record reader");
    return StringToBytes(reader_->GetProtoDesc(channel_name));
  }

  py::bytes get_header_string() {
    EnsureOpen(reader_, "record reader");
    std::string data;
    reader_->GetHeader().SerializeToString(&data);
    return StringToBytes(data);
  }

  void reset() {
    EnsureOpen(reader_, "record reader");
    reader_->Reset();
  }

  std::vector<std::string> get_channel_list() const {
    EnsureOpen(reader_, "record reader");
    const std::set<std::string> channels = reader_->GetChannelList();
    return std::vector<std::string>(channels.begin(), channels.end());
  }

 private:
  std::unique_ptr<record::RecordReader> reader_;
};

class RecordWriterBinding {
 public:
  RecordWriterBinding() : writer_(std::make_unique<record::RecordWriter>()) {}

  void close() {
    if (writer_ == nullptr) {
      return;
    }
    writer_->Close();
    writer_.reset();
  }

  bool open(const std::string& path) {
    EnsureOpen(writer_, "record writer");
    return writer_->Open(path);
  }

  bool write_channel(const std::string& channel_name,
                     const std::string& message_type,
                     const py::bytes& proto_desc) {
    EnsureOpen(writer_, "record writer");
    return writer_->WriteChannel(channel_name, message_type,
                                 BytesToString(proto_desc));
  }

  bool write_message(const std::string& channel_name, const py::bytes& message,
                     uint64_t time, const py::bytes& proto_desc) {
    EnsureOpen(writer_, "record writer");
    return writer_->WriteMessage(
        channel_name,
        std::make_shared<message::RawMessage>(BytesToString(message)), time,
        BytesToString(proto_desc));
  }

  bool set_size_of_file_segmentation(uint64_t size_kilobytes) {
    EnsureOpen(writer_, "record writer");
    return writer_->SetSizeOfFileSegmentation(size_kilobytes);
  }

  bool set_interval_of_file_segmentation(uint64_t time_seconds) {
    EnsureOpen(writer_, "record writer");
    return writer_->SetIntervalOfFileSegmentation(time_seconds);
  }

  uint64_t get_message_number(const std::string& channel_name) const {
    EnsureOpen(writer_, "record writer");
    return writer_->GetMessageNumber(channel_name);
  }

  std::string get_message_type(const std::string& channel_name) const {
    EnsureOpen(writer_, "record writer");
    return writer_->GetMessageType(channel_name);
  }

  py::bytes get_proto_desc(const std::string& channel_name) const {
    EnsureOpen(writer_, "record writer");
    return StringToBytes(writer_->GetProtoDesc(channel_name));
  }

 private:
  std::unique_ptr<record::RecordWriter> writer_;
};

}  // namespace

void RegisterRecordBindings(py::module_& m) {
  py::class_<RecordMessageBinding>(m, "RecordMessage")
      .def_property_readonly("timestamp",
                             [](const RecordMessageBinding& msg) {
                               return msg.timestamp;
                             })
      .def_property_readonly("channel_name",
                             [](const RecordMessageBinding& msg) {
                               return msg.channel_name;
                             })
      .def_property_readonly("data",
                             [](const RecordMessageBinding& msg) {
                               return py::bytes(msg.data);
                             })
      .def_property_readonly("data_type",
                             [](const RecordMessageBinding& msg) {
                               return msg.data_type;
                             })
      .def_property_readonly("end",
                             [](const RecordMessageBinding& msg) {
                               return msg.end;
                             });

  py::class_<RecordReaderBinding, std::shared_ptr<RecordReaderBinding>>(
      m, "RecordReader")
      .def(py::init<const std::string&>())
      .def("close", &RecordReaderBinding::close)
      .def("read_message", &RecordReaderBinding::read_message,
           py::arg("begin_time") = 0,
           py::arg("end_time") = std::numeric_limits<uint64_t>::max())
      .def("get_message_number", &RecordReaderBinding::get_message_number)
      .def("get_message_type", &RecordReaderBinding::get_message_type)
      .def("get_proto_desc", &RecordReaderBinding::get_proto_desc)
      .def("get_header_string", &RecordReaderBinding::get_header_string)
      .def("reset", &RecordReaderBinding::reset)
      .def("get_channel_list", &RecordReaderBinding::get_channel_list);

  py::class_<RecordWriterBinding, std::shared_ptr<RecordWriterBinding>>(
      m, "RecordWriter")
      .def(py::init<>())
      .def("close", &RecordWriterBinding::close)
      .def("open", &RecordWriterBinding::open)
      .def("write_channel", &RecordWriterBinding::write_channel)
      .def("write_message", &RecordWriterBinding::write_message,
           py::arg("channel_name"), py::arg("message"), py::arg("time"),
           py::arg("proto_desc") = py::bytes(""))
      .def("set_size_of_file_segmentation",
           &RecordWriterBinding::set_size_of_file_segmentation)
      .def("set_interval_of_file_segmentation",
           &RecordWriterBinding::set_interval_of_file_segmentation)
      .def("get_message_number", &RecordWriterBinding::get_message_number)
      .def("get_message_type", &RecordWriterBinding::get_message_type)
      .def("get_proto_desc", &RecordWriterBinding::get_proto_desc);
}

}  // namespace cyber
}  // namespace apollo
