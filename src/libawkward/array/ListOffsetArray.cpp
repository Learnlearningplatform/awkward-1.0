// BSD 3-Clause License; see https://github.com/jpivarski/awkward-1.0/blob/master/LICENSE

#include <algorithm>
#include <numeric>
#include <sstream>
#include <type_traits>

#include "awkward/cpu-kernels/identities.h"
#include "awkward/cpu-kernels/getitem.h"
#include "awkward/cpu-kernels/operations.h"
#include "awkward/cpu-kernels/reducers.h"
#include "awkward/cpu-kernels/sorting.h"
#include "awkward/type/ListType.h"
#include "awkward/type/ArrayType.h"
#include "awkward/type/UnknownType.h"
#include "awkward/Slice.h"
#include "awkward/array/ListArray.h"
#include "awkward/array/RegularArray.h"
#include "awkward/array/NumpyArray.h"
#include "awkward/array/EmptyArray.h"
#include "awkward/array/IndexedArray.h"
#include "awkward/array/UnionArray.h"
#include "awkward/array/RecordArray.h"
#include "awkward/array/ByteMaskedArray.h"
#include "awkward/array/BitMaskedArray.h"
#include "awkward/array/UnmaskedArray.h"
#include "awkward/array/VirtualArray.h"

#define AWKWARD_LISTOFFSETARRAY_NO_EXTERN_TEMPLATE
#include "awkward/array/ListOffsetArray.h"

namespace awkward {
  ////////// ListOffsetForm

  ListOffsetForm::ListOffsetForm(bool has_identities,
                                 const util::Parameters& parameters,
                                 Index::Form offsets,
                                 const FormPtr& content)
      : Form(has_identities, parameters)
      , offsets_(offsets)
      , content_(content) { }

  Index::Form
  ListOffsetForm::offsets() const {
    return offsets_;
  }

  const FormPtr
  ListOffsetForm::content() const {
    return content_;
  }

  const TypePtr
  ListOffsetForm::type(const util::TypeStrs& typestrs) const {
    return std::make_shared<ListType>(
               parameters_,
               util::gettypestr(parameters_, typestrs),
               content_.get()->type(typestrs));
  }

  void
  ListOffsetForm::tojson_part(ToJson& builder, bool verbose) const {
    builder.beginrecord();
    builder.field("class");
    if (offsets_ == Index::Form::i32) {
      builder.string("ListOffsetArray32");
    }
    else if (offsets_ == Index::Form::u32) {
      builder.string("ListOffsetArrayU32");
    }
    else if (offsets_ == Index::Form::i64) {
      builder.string("ListOffsetArray64");
    }
    else {
      builder.string("UnrecognizedListOffsetArray");
    }
    builder.field("offsets");
    builder.string(Index::form2str(offsets_));
    builder.field("content");
    content_.get()->tojson_part(builder, verbose);
    identities_tojson(builder, verbose);
    parameters_tojson(builder, verbose);
    builder.endrecord();
  }

  const FormPtr
  ListOffsetForm::shallow_copy() const {
    return std::make_shared<ListOffsetForm>(has_identities_,
                                            parameters_,
                                            offsets_,
                                            content_);
  }

  const std::string
  ListOffsetForm::purelist_parameter(const std::string& key) const {
    std::string out = parameter(key);
    if (out == std::string("null")) {
      return content_.get()->purelist_parameter(key);
    }
    else {
      return out;
    }
  }

  bool
  ListOffsetForm::purelist_isregular() const {
    return false;
  }

  int64_t
  ListOffsetForm::purelist_depth() const {
    return content_.get()->purelist_depth() + 1;
  }

  const std::pair<int64_t, int64_t>
  ListOffsetForm::minmax_depth() const {
    std::pair<int64_t, int64_t> content_depth = content_.get()->minmax_depth();
    return std::pair<int64_t, int64_t>(content_depth.first + 1,
                                       content_depth.second + 1);
  }

  const std::pair<bool, int64_t>
  ListOffsetForm::branch_depth() const {
    std::pair<bool, int64_t> content_depth = content_.get()->branch_depth();
    return std::pair<bool, int64_t>(content_depth.first,
                                    content_depth.second + 1);
  }

  int64_t
  ListOffsetForm::numfields() const {
    return content_.get()->numfields();
  }

  int64_t
  ListOffsetForm::fieldindex(const std::string& key) const {
    return content_.get()->fieldindex(key);
  }

  const std::string
  ListOffsetForm::key(int64_t fieldindex) const {
    return content_.get()->key(fieldindex);
  }

  bool
  ListOffsetForm::haskey(const std::string& key) const {
    return content_.get()->haskey(key);
  }

  const std::vector<std::string>
  ListOffsetForm::keys() const {
    return content_.get()->keys();
  }

  bool
  ListOffsetForm::equal(const FormPtr& other,
                        bool check_identities,
                        bool check_parameters) const {
    if (check_identities  &&
        has_identities_ != other.get()->has_identities()) {
      return false;
    }
    if (check_parameters  &&
        !util::parameters_equal(parameters_, other.get()->parameters())) {
      return false;
    }
    if (ListOffsetForm* t = dynamic_cast<ListOffsetForm*>(other.get())) {
      return (offsets_ == t->offsets()  &&
              content_.get()->equal(t->content(),
                                    check_identities,
                                    check_parameters));
    }
    else {
      return false;
    }
  }

  ////////// ListOffsetArray

  template <typename T>
  ListOffsetArrayOf<T>::ListOffsetArrayOf(const IdentitiesPtr& identities,
                                          const util::Parameters& parameters,
                                          const IndexOf<T>& offsets,
                                          const ContentPtr& content)
      : Content(identities, parameters)
      , offsets_(offsets)
      , content_(content) {
    if (offsets.length() == 0) {
      throw std::invalid_argument(
        "ListOffsetArray offsets length must be at least 1");
    }
  }

  template <typename T>
  const IndexOf<T>
  ListOffsetArrayOf<T>::starts() const {
    return util::make_starts(offsets_);
  }

  template <typename T>
  const IndexOf<T>
  ListOffsetArrayOf<T>::stops() const {
    return util::make_stops(offsets_);
  }

  template <typename T>
  const IndexOf<T>
  ListOffsetArrayOf<T>::offsets() const {
    return offsets_;
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::content() const {
    return content_;
  }

  template <>
  Index64
  ListOffsetArrayOf<int64_t>::compact_offsets64(bool start_at_zero) const {
    if (!start_at_zero  ||
        offsets_.getitem_at_nowrap(0) == 0) {
      return offsets_;
    }
    else {
      int64_t len = offsets_.length() - 1;
      Index64 out(len + 1);
      struct Error err =
        util::awkward_listoffsetarray_compact_offsets64<int64_t>(
        out.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        len);
      util::handle_error(err, classname(), identities_.get());
      return out;
    }
  }

  template <typename T>
  Index64
  ListOffsetArrayOf<T>::compact_offsets64(bool start_at_zero) const {
    int64_t len = offsets_.length() - 1;
    Index64 out(len + 1);
    struct Error err = util::awkward_listoffsetarray_compact_offsets64<T>(
      out.ptr().get(),
      offsets_.ptr().get(),
      offsets_.offset(),
      len);
    util::handle_error(err, classname(), identities_.get());
    return out;
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::broadcast_tooffsets64(const Index64& offsets) const {
    if (offsets.length() == 0  ||  offsets.getitem_at_nowrap(0) != 0) {
      throw std::invalid_argument(
        "broadcast_tooffsets64 can only be used with offsets that start at 0");
    }
    if (offsets.length() - 1 > offsets_.length() - 1) {
      throw std::invalid_argument(
        std::string("cannot broadcast ListOffsetArray of length ")
        + std::to_string(offsets_.length() - 1) + (" to length ")
        + std::to_string(offsets.length() - 1));
    }

    IndexOf<T> starts = util::make_starts(offsets_);
    IndexOf<T> stops = util::make_stops(offsets_);

    int64_t carrylen = offsets.getitem_at_nowrap(offsets.length() - 1);
    Index64 nextcarry(carrylen);
    struct Error err = util::awkward_listarray_broadcast_tooffsets64<T>(
      nextcarry.ptr().get(),
      offsets.ptr().get(),
      offsets.offset(),
      offsets.length(),
      starts.ptr().get(),
      starts.offset(),
      stops.ptr().get(),
      stops.offset(),
      content_.get()->length());
    util::handle_error(err, classname(), identities_.get());

    ContentPtr nextcontent = content_.get()->carry(nextcarry);

    IdentitiesPtr identities;
    if (identities_.get() != nullptr) {
      identities =
        identities_.get()->getitem_range_nowrap(0, offsets.length() - 1);
    }
    return std::make_shared<ListOffsetArray64>(identities,
                                               parameters_,
                                               offsets,
                                               nextcontent);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::toRegularArray() const {
    int64_t start = (int64_t)offsets_.getitem_at(0);
    int64_t stop = (int64_t)offsets_.getitem_at(offsets_.length() - 1);
    ContentPtr content = content_.get()->getitem_range_nowrap(start, stop);

    int64_t size;
    struct Error err = util::awkward_listoffsetarray_toRegularArray<T>(
      &size,
      offsets_.ptr().get(),
      offsets_.offset(),
      offsets_.length());
    util::handle_error(err, classname(), identities_.get());

    return std::make_shared<RegularArray>(identities_,
                                          parameters_,
                                          content,
                                          size);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::toListOffsetArray64(bool start_at_zero) const {
    if (std::is_same<T, int64_t>::value  &&
        (!start_at_zero  ||
         offsets_.getitem_at_nowrap(0) == 0)) {
      return shallow_copy();
    }
    else {
      Index64 offsets = compact_offsets64(start_at_zero);
      return broadcast_tooffsets64(offsets);
    }
  }

  template <typename T>
  const std::string
  ListOffsetArrayOf<T>::classname() const {
    if (std::is_same<T, int32_t>::value) {
      return "ListOffsetArray32";
    }
    else if (std::is_same<T, uint32_t>::value) {
      return "ListOffsetArrayU32";
    }
    else if (std::is_same<T, int64_t>::value) {
      return "ListOffsetArray64";
    }
    else {
      return "UnrecognizedListOffsetArray";
    }
  }

  template <typename T>
  void
  ListOffsetArrayOf<T>::setidentities(const IdentitiesPtr& identities) {
    if (identities.get() == nullptr) {
      content_.get()->setidentities(identities);
    }
    else {
      if (length() != identities.get()->length()) {
        util::handle_error(failure(
          "content and its identities must have the same length",
          kSliceNone,
          kSliceNone),
        classname(),
        identities_.get());
      }
      IdentitiesPtr bigidentities = identities;
      if (content_.get()->length() > kMaxInt32  ||
          !std::is_same<T, int32_t>::value) {
        bigidentities = identities.get()->to64();
      }
      if (Identities32* rawidentities =
          dynamic_cast<Identities32*>(bigidentities.get())) {
        IdentitiesPtr subidentities =
          std::make_shared<Identities32>(Identities::newref(),
                                         rawidentities->fieldloc(),
                                         rawidentities->width() + 1,
                                         content_.get()->length());
        Identities32* rawsubidentities =
          reinterpret_cast<Identities32*>(subidentities.get());
        struct Error err = util::awkward_identities32_from_listoffsetarray<T>(
          rawsubidentities->ptr().get(),
          rawidentities->ptr().get(),
          offsets_.ptr().get(),
          rawidentities->offset(),
          offsets_.offset(),
          content_.get()->length(),
          length(),
          rawidentities->width());
        util::handle_error(err, classname(), identities_.get());
        content_.get()->setidentities(subidentities);
      }
      else if (Identities64* rawidentities =
               dynamic_cast<Identities64*>(bigidentities.get())) {
        IdentitiesPtr subidentities =
          std::make_shared<Identities64>(Identities::newref(),
                                         rawidentities->fieldloc(),
                                         rawidentities->width() + 1,
                                         content_.get()->length());
        Identities64* rawsubidentities =
          reinterpret_cast<Identities64*>(subidentities.get());
        struct Error err = util::awkward_identities64_from_listoffsetarray<T>(
          rawsubidentities->ptr().get(),
          rawidentities->ptr().get(),
          offsets_.ptr().get(),
          rawidentities->offset(),
          offsets_.offset(),
          content_.get()->length(),
          length(),
          rawidentities->width());
        util::handle_error(err, classname(), identities_.get());
        content_.get()->setidentities(subidentities);
      }
      else {
        throw std::runtime_error("unrecognized Identities specialization");
      }
    }
    identities_ = identities;
  }

  template <typename T>
  void
  ListOffsetArrayOf<T>::setidentities() {
    if (length() <= kMaxInt32) {
      IdentitiesPtr newidentities =
        std::make_shared<Identities32>(Identities::newref(),
                                       Identities::FieldLoc(),
                                       1,
                                       length());
      Identities32* rawidentities =
        reinterpret_cast<Identities32*>(newidentities.get());
      struct Error err = awkward_new_identities32(rawidentities->ptr().get(),
                                                  length());
      util::handle_error(err, classname(), identities_.get());
      setidentities(newidentities);
    }
    else {
      IdentitiesPtr newidentities =
        std::make_shared<Identities64>(Identities::newref(),
                                       Identities::FieldLoc(),
                                       1,
                                       length());
      Identities64* rawidentities =
        reinterpret_cast<Identities64*>(newidentities.get());
      struct Error err = awkward_new_identities64(rawidentities->ptr().get(),
                                                  length());
      util::handle_error(err, classname(), identities_.get());
      setidentities(newidentities);
    }
  }

  template <typename T>
  const TypePtr
  ListOffsetArrayOf<T>::type(const util::TypeStrs& typestrs) const {
    return form(true).get()->type(typestrs);
  }

  template <typename T>
  const FormPtr
  ListOffsetArrayOf<T>::form(bool materialize) const {
    return std::make_shared<ListOffsetForm>(identities_.get() != nullptr,
                                            parameters_,
                                            offsets_.form(),
                                            content_.get()->form(materialize));
  }

  template <typename T>
  bool
  ListOffsetArrayOf<T>::has_virtual_form() const {
    return content_.get()->has_virtual_form();
  }

  template <typename T>
  bool
  ListOffsetArrayOf<T>::has_virtual_length() const {
    return content_.get()->has_virtual_length();
  }

  template <typename T>
  const std::string
  ListOffsetArrayOf<T>::tostring_part(const std::string& indent,
                                      const std::string& pre,
                                      const std::string& post) const {
    std::stringstream out;
    out << indent << pre << "<" << classname() << ">\n";
    if (identities_.get() != nullptr) {
      out << identities_.get()->tostring_part(indent + std::string("    "),
                                              "",
                                              "\n");
    }
    if (!parameters_.empty()) {
      out << parameters_tostring(indent + std::string("    "), "", "\n");
    }
    out << offsets_.tostring_part(
             indent + std::string("    "), "<offsets>", "</offsets>\n");
    out << content_.get()->tostring_part(
             indent + std::string("    "), "<content>", "</content>\n");
    out << indent << "</" << classname() << ">" << post;
    return out.str();
  }

  template <typename T>
  void
  ListOffsetArrayOf<T>::tojson_part(ToJson& builder,
                                    bool include_beginendlist) const {
    int64_t len = length();
    check_for_iteration();
    if (include_beginendlist) {
      builder.beginlist();
    }
    for (int64_t i = 0;  i < len;  i++) {
      getitem_at_nowrap(i).get()->tojson_part(builder, true);
    }
    if (include_beginendlist) {
      builder.endlist();
    }
  }

  template <typename T>
  void
  ListOffsetArrayOf<T>::nbytes_part(std::map<size_t, int64_t>& largest) const {
    offsets_.nbytes_part(largest);
    content_.get()->nbytes_part(largest);
    if (identities_.get() != nullptr) {
      identities_.get()->nbytes_part(largest);
    }
  }

  template <typename T>
  int64_t
  ListOffsetArrayOf<T>::length() const {
    return offsets_.length() - 1;
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::shallow_copy() const {
    return std::make_shared<ListOffsetArrayOf<T>>(identities_,
                                                  parameters_,
                                                  offsets_,
                                                  content_);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::deep_copy(bool copyarrays,
                                  bool copyindexes,
                                  bool copyidentities) const {
    IndexOf<T> offsets = copyindexes ? offsets_.deep_copy() : offsets_;
    ContentPtr content = content_.get()->deep_copy(copyarrays,
                                                   copyindexes,
                                                   copyidentities);
    IdentitiesPtr identities = identities_;
    if (copyidentities  &&  identities_.get() != nullptr) {
      identities = identities_.get()->deep_copy();
    }
    return std::make_shared<ListOffsetArrayOf<T>>(identities,
                                                  parameters_,
                                                  offsets,
                                                  content);
  }

  template <typename T>
  void
  ListOffsetArrayOf<T>::check_for_iteration() const {
    if (identities_.get() != nullptr  &&
        identities_.get()->length() < offsets_.length() - 1) {
      util::handle_error(failure(
        "len(identities) < len(array)", kSliceNone, kSliceNone),
        identities_.get()->classname(),
        nullptr);
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_nothing() const {
    return content_.get()->getitem_range_nowrap(0, 0);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_at(int64_t at) const {
    int64_t regular_at = at;
    if (regular_at < 0) {
      regular_at += offsets_.length() - 1;
    }
    if (!(0 <= regular_at  &&  regular_at < offsets_.length() - 1)) {
      util::handle_error(failure("index out of range", kSliceNone, at),
                         classname(),
                         identities_.get());
    }
    return getitem_at_nowrap(regular_at);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_at_nowrap(int64_t at) const {
    int64_t start = (int64_t)offsets_.getitem_at_nowrap(at);
    int64_t stop = (int64_t)offsets_.getitem_at_nowrap(at + 1);
    int64_t lencontent = content_.get()->length();
    if (start == stop) {
      start = stop = 0;
    }
    if (start < 0) {
      util::handle_error(failure(
                           "offsets[i] < 0", kSliceNone, at),
                         classname(),
                         identities_.get());
    }
    if (start > stop) {
      util::handle_error(failure(
                           "offsets[i] > offsets[i + 1]", kSliceNone, at),
                         classname(),
                         identities_.get());
    }
    if (stop > lencontent) {
      util::handle_error(failure(
                           "offsets[i] != offsets[i + 1] and "
                           "offsets[i + 1] > len(content)", kSliceNone, at),
                         classname(),
                         identities_.get());
    }
    return content_.get()->getitem_range_nowrap(start, stop);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_range(int64_t start, int64_t stop) const {
    int64_t regular_start = start;
    int64_t regular_stop = stop;
    awkward_regularize_rangeslice(&regular_start, &regular_stop,
      true, start != Slice::none(), stop != Slice::none(),
      offsets_.length() - 1);
    if (identities_.get() != nullptr  &&
        regular_stop > identities_.get()->length()) {
      util::handle_error(failure("index out of range", kSliceNone, stop),
                         identities_.get()->classname(),
                         nullptr);
    }
    return getitem_range_nowrap(regular_start, regular_stop);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_range_nowrap(int64_t start,
                                             int64_t stop) const {
    IdentitiesPtr identities(nullptr);
    if (identities_.get() != nullptr) {
      identities = identities_.get()->getitem_range_nowrap(start, stop);
    }
    return std::make_shared<ListOffsetArrayOf<T>>(
             identities,
             parameters_,
             offsets_.getitem_range_nowrap(start, stop + 1),
             content_);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_field(const std::string& key) const {
    return std::make_shared<ListOffsetArrayOf<T>>(
             identities_,
             util::Parameters(),
             offsets_,
             content_.get()->getitem_field(key));
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_fields(
    const std::vector<std::string>& keys) const {
    return std::make_shared<ListOffsetArrayOf<T>>(
      identities_,
      util::Parameters(),
      offsets_,
      content_.get()->getitem_fields(keys));
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next_jagged(const Index64& slicestarts,
                                            const Index64& slicestops,
                                            const SliceItemPtr& slicecontent,
                                            const Slice& tail) const {
    ContentPtr listarray = std::make_shared<ListArrayOf<T>>(
                             identities_,
                             parameters_,
                             util::make_starts(offsets_),
                             util::make_stops(offsets_),
                             content_);
    return listarray.get()->getitem_next_jagged(slicestarts,
                                                slicestops,
                                                slicecontent,
                                                tail);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::carry(const Index64& carry) const {
    IndexOf<T> starts = util::make_starts(offsets_);
    IndexOf<T> stops = util::make_stops(offsets_);
    IndexOf<T> nextstarts(carry.length());
    IndexOf<T> nextstops(carry.length());
    struct Error err = util::awkward_listarray_getitem_carry_64<T>(
      nextstarts.ptr().get(),
      nextstops.ptr().get(),
      starts.ptr().get(),
      stops.ptr().get(),
      carry.ptr().get(),
      starts.offset(),
      stops.offset(),
      offsets_.length() - 1,
      carry.length());
    util::handle_error(err, classname(), identities_.get());
    IdentitiesPtr identities(nullptr);
    if (identities_.get() != nullptr) {
      identities = identities_.get()->getitem_carry_64(carry);
    }
    return std::make_shared<ListArrayOf<T>>(identities,
                                            parameters_,
                                            nextstarts,
                                            nextstops,
                                            content_);
  }

  template <typename T>
  int64_t
  ListOffsetArrayOf<T>::numfields() const {
    return content_.get()->numfields();
  }

  template <typename T>
  int64_t
  ListOffsetArrayOf<T>::fieldindex(const std::string& key) const {
    return content_.get()->fieldindex(key);
  }

  template <typename T>
  const std::string
  ListOffsetArrayOf<T>::key(int64_t fieldindex) const {
    return content_.get()->key(fieldindex);
  }

  template <typename T>
  bool
  ListOffsetArrayOf<T>::haskey(const std::string& key) const {
    return content_.get()->haskey(key);
  }

  template <typename T>
  const std::vector<std::string>
  ListOffsetArrayOf<T>::keys() const {
    return content_.get()->keys();
  }

  template <typename T>
  const std::string
  ListOffsetArrayOf<T>::validityerror(const std::string& path) const {
    IndexOf<T> starts = util::make_starts(offsets_);
    IndexOf<T> stops = util::make_stops(offsets_);
    struct Error err = util::awkward_listarray_validity<T>(
      starts.ptr().get(),
      starts.offset(),
      stops.ptr().get(),
      stops.offset(),
      starts.length(),
      content_.get()->length());
    if (err.str == nullptr) {
      return content_.get()->validityerror(path + std::string(".content"));
    }
    else {
      return (std::string("at ") + path + std::string(" (") + classname()
              + std::string("): ") + std::string(err.str)
              + std::string(" at i=") + std::to_string(err.identity));
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::shallow_simplify() const {
    return shallow_copy();
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::num(int64_t axis, int64_t depth) const {
    int64_t toaxis = axis_wrap_if_negative(axis);
    if (toaxis == depth) {
      Index64 out(1);
      out.setitem_at_nowrap(0, length());
      return NumpyArray(out).getitem_at_nowrap(0);
    }
    else if (toaxis == depth + 1) {
      IndexOf<T> starts = util::make_starts(offsets_);
      IndexOf<T> stops = util::make_stops(offsets_);
      Index64 tonum(length());
      struct Error err = util::awkward_listarray_num_64<T>(
        tonum.ptr().get(),
        starts.ptr().get(),
        starts.offset(),
        stops.ptr().get(),
        stops.offset(),
        length());
      util::handle_error(err, classname(), identities_.get());
      return std::make_shared<NumpyArray>(tonum);
    }
    else {
      ContentPtr next = content_.get()->num(axis, depth + 1);
      Index64 offsets = compact_offsets64(true);
      return std::make_shared<ListOffsetArray64>(Identities::none(),
                                                 util::Parameters(),
                                                 offsets,
                                                 next);
    }
  }

  template <typename T>
  const std::pair<Index64, ContentPtr>
  ListOffsetArrayOf<T>::offsets_and_flattened(int64_t axis,
                                              int64_t depth) const {
    int64_t toaxis = axis_wrap_if_negative(axis);
    if (toaxis == depth) {
      throw std::invalid_argument("axis=0 not allowed for flatten");
    }
    else if (toaxis == depth + 1) {
      ContentPtr listoffsetarray = toListOffsetArray64(true);
      ListOffsetArray64* raw =
        dynamic_cast<ListOffsetArray64*>(listoffsetarray.get());
      return std::pair<Index64, ContentPtr>(raw->offsets(), raw->content());
    }
    else {
      std::pair<Index64, ContentPtr> pair =
        content_.get()->offsets_and_flattened(axis, depth + 1);
      Index64 inneroffsets = pair.first;
      if (inneroffsets.length() == 0) {
        return std::pair<Index64, ContentPtr>(
                 Index64(0),
                 std::make_shared<ListOffsetArrayOf<T>>(Identities::none(),
                                                        util::Parameters(),
                                                        offsets_,
                                                        pair.second));
      }
      else {
        Index64 tooffsets(offsets_.length());
        struct Error err = util::awkward_listoffsetarray_flatten_offsets_64<T>(
          tooffsets.ptr().get(),
          offsets_.ptr().get(),
          offsets_.offset(),
          offsets_.length(),
          inneroffsets.ptr().get(),
          inneroffsets.offset(),
          inneroffsets.length());
        util::handle_error(err, classname(), identities_.get());
        return std::pair<Index64, ContentPtr>(
                 Index64(0),
                 std::make_shared<ListOffsetArray64>(Identities::none(),
                                                     util::Parameters(),
                                                     tooffsets,
                                                     pair.second));
      }
    }
  }

  template <typename T>
  bool
  ListOffsetArrayOf<T>::mergeable(const ContentPtr& other,
                                  bool mergebool) const {
    if (VirtualArray* raw = dynamic_cast<VirtualArray*>(other.get())) {
      return mergeable(raw->array(), mergebool);
    }

    if (!parameters_equal(other.get()->parameters())) {
      return false;
    }

    if (dynamic_cast<EmptyArray*>(other.get())  ||
        dynamic_cast<UnionArray8_32*>(other.get())  ||
        dynamic_cast<UnionArray8_U32*>(other.get())  ||
        dynamic_cast<UnionArray8_64*>(other.get())) {
      return true;
    }
    else if (IndexedArray32* rawother =
             dynamic_cast<IndexedArray32*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }
    else if (IndexedArrayU32* rawother =
             dynamic_cast<IndexedArrayU32*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }
    else if (IndexedArray64* rawother =
             dynamic_cast<IndexedArray64*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }
    else if (IndexedOptionArray32* rawother =
             dynamic_cast<IndexedOptionArray32*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }
    else if (IndexedOptionArray64* rawother =
             dynamic_cast<IndexedOptionArray64*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }
    else if (ByteMaskedArray* rawother =
             dynamic_cast<ByteMaskedArray*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }
    else if (BitMaskedArray* rawother =
             dynamic_cast<BitMaskedArray*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }
    else if (UnmaskedArray* rawother =
             dynamic_cast<UnmaskedArray*>(other.get())) {
      return mergeable(rawother->content(), mergebool);
    }

    if (RegularArray* rawother =
        dynamic_cast<RegularArray*>(other.get())) {
      return content_.get()->mergeable(rawother->content(), mergebool);
    }
    else if (ListArray32* rawother =
             dynamic_cast<ListArray32*>(other.get())) {
      return content_.get()->mergeable(rawother->content(), mergebool);
    }
    else if (ListArrayU32* rawother =
             dynamic_cast<ListArrayU32*>(other.get())) {
      return content_.get()->mergeable(rawother->content(), mergebool);
    }
    else if (ListArray64* rawother =
             dynamic_cast<ListArray64*>(other.get())) {
      return content_.get()->mergeable(rawother->content(), mergebool);
    }
    else if (ListOffsetArray32* rawother =
             dynamic_cast<ListOffsetArray32*>(other.get())) {
      return content_.get()->mergeable(rawother->content(), mergebool);
    }
    else if (ListOffsetArrayU32* rawother =
             dynamic_cast<ListOffsetArrayU32*>(other.get())) {
      return content_.get()->mergeable(rawother->content(), mergebool);
    }
    else if (ListOffsetArray64* rawother =
             dynamic_cast<ListOffsetArray64*>(other.get())) {
      return content_.get()->mergeable(rawother->content(), mergebool);
    }
    else {
      return false;
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::merge(const ContentPtr& other) const {
    if (VirtualArray* raw = dynamic_cast<VirtualArray*>(other.get())) {
      return merge(raw->array());
    }

    if (!parameters_equal(other.get()->parameters())) {
      return merge_as_union(other);
    }

    if (dynamic_cast<EmptyArray*>(other.get())) {
      return shallow_copy();
    }
    else if (IndexedArray32* rawother =
             dynamic_cast<IndexedArray32*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (IndexedArrayU32* rawother =
             dynamic_cast<IndexedArrayU32*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (IndexedArray64* rawother =
             dynamic_cast<IndexedArray64*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (IndexedOptionArray32* rawother =
             dynamic_cast<IndexedOptionArray32*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (IndexedOptionArray64* rawother =
             dynamic_cast<IndexedOptionArray64*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (ByteMaskedArray* rawother =
             dynamic_cast<ByteMaskedArray*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (BitMaskedArray* rawother =
             dynamic_cast<BitMaskedArray*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (UnmaskedArray* rawother =
             dynamic_cast<UnmaskedArray*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (UnionArray8_32* rawother =
             dynamic_cast<UnionArray8_32*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (UnionArray8_U32* rawother =
             dynamic_cast<UnionArray8_U32*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }
    else if (UnionArray8_64* rawother =
             dynamic_cast<UnionArray8_64*>(other.get())) {
      return rawother->reverse_merge(shallow_copy());
    }

    int64_t mylength = length();
    int64_t theirlength = other.get()->length();
    Index64 starts(mylength + theirlength);
    Index64 stops(mylength + theirlength);

    IndexOf<T> self_starts = util::make_starts(offsets_);
    IndexOf<T> self_stops = util::make_stops(offsets_);

    if (std::is_same<T, int32_t>::value) {
      struct Error err = awkward_listarray_fill_to64_from32(
        starts.ptr().get(),
        0,
        stops.ptr().get(),
        0,
        reinterpret_cast<int32_t*>(self_starts.ptr().get()),
        self_starts.offset(),
        reinterpret_cast<int32_t*>(self_stops.ptr().get()),
        self_stops.offset(),
        mylength,
        0);
      util::handle_error(err, classname(), identities_.get());
    }
    else if (std::is_same<T, uint32_t>::value) {
      struct Error err = awkward_listarray_fill_to64_fromU32(
        starts.ptr().get(),
        0,
        stops.ptr().get(),
        0,
        reinterpret_cast<uint32_t*>(self_starts.ptr().get()),
        self_starts.offset(),
        reinterpret_cast<uint32_t*>(self_stops.ptr().get()),
        self_stops.offset(),
        mylength,
        0);
      util::handle_error(err, classname(), identities_.get());
    }
    else if (std::is_same<T, int64_t>::value) {
      struct Error err = awkward_listarray_fill_to64_from64(
        starts.ptr().get(),
        0,
        stops.ptr().get(),
        0,
        reinterpret_cast<int64_t*>(self_starts.ptr().get()),
        self_starts.offset(),
        reinterpret_cast<int64_t*>(self_stops.ptr().get()),
        self_stops.offset(),
        mylength,
        0);
      util::handle_error(err, classname(), identities_.get());
    }
    else {
      throw std::runtime_error("unrecognized ListOffsetArray specialization");
    }

    int64_t mycontentlength = content_.get()->length();
    ContentPtr content;
    if (ListArray32* rawother =
        dynamic_cast<ListArray32*>(other.get())) {
      content = content_.get()->merge(rawother->content());
      Index32 other_starts = rawother->starts();
      Index32 other_stops = rawother->stops();
      struct Error err = awkward_listarray_fill_to64_from32(
        starts.ptr().get(),
        mylength,
        stops.ptr().get(),
        mylength,
        other_starts.ptr().get(),
        other_starts.offset(),
        other_stops.ptr().get(),
        other_stops.offset(),
        theirlength,
        mycontentlength);
      util::handle_error(err,
                         rawother->classname(),
                         rawother->identities().get());
    }
    else if (ListArrayU32* rawother =
             dynamic_cast<ListArrayU32*>(other.get())) {
      content = content_.get()->merge(rawother->content());
      IndexU32 other_starts = rawother->starts();
      IndexU32 other_stops = rawother->stops();
      struct Error err = awkward_listarray_fill_to64_fromU32(
        starts.ptr().get(),
        mylength,
        stops.ptr().get(),
        mylength,
        other_starts.ptr().get(),
        other_starts.offset(),
        other_stops.ptr().get(),
        other_stops.offset(),
        theirlength,
        mycontentlength);
      util::handle_error(err,
                         rawother->classname(),
                         rawother->identities().get());
    }
    else if (ListArray64* rawother =
             dynamic_cast<ListArray64*>(other.get())) {
      content = content_.get()->merge(rawother->content());
      Index64 other_starts = rawother->starts();
      Index64 other_stops = rawother->stops();
      struct Error err = awkward_listarray_fill_to64_from64(
        starts.ptr().get(),
        mylength,
        stops.ptr().get(),
        mylength,
        other_starts.ptr().get(),
        other_starts.offset(),
        other_stops.ptr().get(),
        other_stops.offset(),
        theirlength,
        mycontentlength);
      util::handle_error(err,
                         rawother->classname(),
                         rawother->identities().get());
    }
    else if (ListOffsetArray32* rawother =
             dynamic_cast<ListOffsetArray32*>(other.get())) {
      content = content_.get()->merge(rawother->content());
      Index32 other_starts = rawother->starts();
      Index32 other_stops = rawother->stops();
      struct Error err = awkward_listarray_fill_to64_from32(
        starts.ptr().get(),
        mylength,
        stops.ptr().get(),
        mylength,
        other_starts.ptr().get(),
        other_starts.offset(),
        other_stops.ptr().get(),
        other_stops.offset(),
        theirlength,
        mycontentlength);
      util::handle_error(err,
                         rawother->classname(),
                         rawother->identities().get());
    }
    else if (ListOffsetArrayU32* rawother =
             dynamic_cast<ListOffsetArrayU32*>(other.get())) {
      content = content_.get()->merge(rawother->content());
      IndexU32 other_starts = rawother->starts();
      IndexU32 other_stops = rawother->stops();
      struct Error err = awkward_listarray_fill_to64_fromU32(
        starts.ptr().get(),
        mylength,
        stops.ptr().get(),
        mylength,
        other_starts.ptr().get(),
        other_starts.offset(),
        other_stops.ptr().get(),
        other_stops.offset(),
        theirlength,
        mycontentlength);
      util::handle_error(err,
                         rawother->classname(),
                         rawother->identities().get());
    }
    else if (ListOffsetArray64* rawother =
             dynamic_cast<ListOffsetArray64*>(other.get())) {
      content = content_.get()->merge(rawother->content());
      Index64 other_starts = rawother->starts();
      Index64 other_stops = rawother->stops();
      struct Error err = awkward_listarray_fill_to64_from64(
        starts.ptr().get(),
        mylength,
        stops.ptr().get(),
        mylength,
        other_starts.ptr().get(),
        other_starts.offset(),
        other_stops.ptr().get(),
        other_stops.offset(),
        theirlength,
        mycontentlength);
      util::handle_error(err,
                         rawother->classname(),
                         rawother->identities().get());
    }
    else if (RegularArray* rawregulararray =
             dynamic_cast<RegularArray*>(other.get())) {
      ContentPtr listoffsetarray = rawregulararray->toListOffsetArray64(true);
      ListOffsetArray64* rawother =
        dynamic_cast<ListOffsetArray64*>(listoffsetarray.get());
      content = content_.get()->merge(rawother->content());
      Index64 other_starts = rawother->starts();
      Index64 other_stops = rawother->stops();
      struct Error err = awkward_listarray_fill_to64_from64(
        starts.ptr().get(),
        mylength,
        stops.ptr().get(),
        mylength,
        other_starts.ptr().get(),
        other_starts.offset(),
        other_stops.ptr().get(),
        other_stops.offset(),
        theirlength,
        mycontentlength);
      util::handle_error(err,
                         rawother->classname(),
                         rawother->identities().get());
    }
    else {
      throw std::invalid_argument(std::string("cannot merge ") + classname()
                                  + std::string(" with ")
                                  + other.get()->classname());
    }

    return std::make_shared<ListArray64>(Identities::none(),
                                         util::Parameters(),
                                         starts,
                                         stops,
                                         content);
  }

  template <>
  const SliceItemPtr ListOffsetArrayOf<int64_t>::asslice() const {
    int64_t start = offsets_.getitem_at_nowrap(0);
    int64_t stop = offsets_.getitem_at_nowrap(offsets_.length() - 1);
    ContentPtr next = content_.get()->getitem_range_nowrap(start, stop);

    std::shared_ptr<Index64> offsets = std::make_shared<Index64>(
      offsets_.ptr(), offsets_.offset(), offsets_.length());
    if (start != 0) {
      offsets = std::make_shared<Index64>(offsets_.length());
      struct Error err = awkward_listoffsetarray64_compact_offsets64(
        offsets.get()->ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        length());
      util::handle_error(err, classname(), identities_.get());
    }

    SliceItemPtr slicecontent = next.get()->asslice();
    if (SliceArray64* array =
        dynamic_cast<SliceArray64*>(slicecontent.get())) {
      if (array->frombool()) {
        Index64 nonzero(array->index());
        Index64 adjustedoffsets(offsets.get()->length());
        Index64 adjustednonzero(nonzero.length());

        struct Error err = awkward_listoffsetarray_getitem_adjust_offsets_64(
          adjustedoffsets.ptr().get(),
          adjustednonzero.ptr().get(),
          offsets.get()->ptr().get(),
          offsets.get()->offset(),
          offsets.get()->length() - 1,
          nonzero.ptr().get(),
          nonzero.offset(),
          nonzero.length());
        util::handle_error(err, classname(), nullptr);

        SliceItemPtr newarray = std::make_shared<SliceArray64>(
          adjustednonzero, array->shape(), array->strides(), true);
        return std::make_shared<SliceJagged64>(adjustedoffsets, newarray);
      }
    }
    else if (SliceMissing64* missing =
             dynamic_cast<SliceMissing64*>(slicecontent.get())) {
      if (SliceArray64* array =
          dynamic_cast<SliceArray64*>(missing->content().get())) {
        if (array->frombool()) {
          Index8 originalmask = missing->originalmask();
          Index64 index = missing->index();
          Index64 nonzero = array->index();
          Index64 adjustedoffsets(offsets.get()->length());
          Index64 adjustednonzero(nonzero.length());

          struct Error err =
            awkward_listoffsetarray_getitem_adjust_offsets_index_64(
            adjustedoffsets.ptr().get(),
            adjustednonzero.ptr().get(),
            offsets.get()->ptr().get(),
            offsets.get()->offset(),
            offsets.get()->length() - 1,
            index.ptr().get(),
            index.offset(),
            index.length(),
            nonzero.ptr().get(),
            nonzero.offset(),
            nonzero.length(),
            originalmask.ptr().get(),
            originalmask.offset(),
            originalmask.length());
          util::handle_error(err, classname(), nullptr);

          SliceItemPtr newarray = std::make_shared<SliceArray64>(
            adjustednonzero, array->shape(), array->strides(), true);
          SliceItemPtr newmissing = std::make_shared<SliceMissing64>(
            missing->index(), missing->originalmask(), newarray);
          return std::make_shared<SliceJagged64>(adjustedoffsets, newmissing);
        }
      }
    }
    return std::make_shared<SliceJagged64>(Index64(offsets.get()->ptr(),
                                                   offsets.get()->offset(),
                                                   offsets.get()->length()),
                                           slicecontent);
  }

  template <typename T>
  const SliceItemPtr
  ListOffsetArrayOf<T>::asslice() const {
    return toListOffsetArray64(true).get()->asslice();
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::fillna(const ContentPtr& value) const {
    return std::make_shared<ListOffsetArrayOf<T>>(
      identities_, parameters_, offsets_, content().get()->fillna(value));
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::rpad(int64_t target,
                             int64_t axis,
                             int64_t depth) const {
    int64_t toaxis = axis_wrap_if_negative(axis);
    if (toaxis == depth) {
      return rpad_axis0(target, false);
    }
    if (toaxis == depth + 1) {
      int64_t tolength = 0;
      IndexOf<T> offsets(offsets_.length());
      struct Error err1 = util::awkward_ListOffsetArray_rpad_length_axis1<T>(
        offsets.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1,
        target,
        &tolength);
      util::handle_error(err1, classname(), identities_.get());

      Index64 outindex(tolength);
      struct Error err2 = util::awkward_ListOffsetArray_rpad_axis1_64<T>(
        outindex.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1,
        target);
      util::handle_error(err2, classname(), identities_.get());

      std::shared_ptr<IndexedOptionArray64> next =
        std::make_shared<IndexedOptionArray64>(identities_,
                                               parameters_,
                                               outindex,
                                               content());
      return std::make_shared<ListOffsetArrayOf<T>>(
        identities_, parameters_, offsets, next.get()->simplify_optiontype());
    }
    else {
      return std::make_shared<ListOffsetArrayOf<T>>(
        Identities::none(), parameters_, offsets_,
        content_.get()->rpad(target, toaxis, depth + 1));
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::rpad_and_clip(int64_t target,
                                      int64_t axis,
                                      int64_t depth) const {
    int64_t toaxis = axis_wrap_if_negative(axis);
    if (toaxis == depth) {
      return rpad_axis0(target, true);
    }
    else if (toaxis == depth + 1) {
      Index64 starts(offsets_.length() - 1);
      Index64 stops(offsets_.length() - 1);

      struct Error err1 = awkward_index_rpad_and_clip_axis1_64(
        starts.ptr().get(),
        stops.ptr().get(),
        target,
        starts.length());
      util::handle_error(err1, classname(), identities_.get());

      Index64 outindex(target*(offsets_.length() - 1));
      struct Error err2 =
        util::awkward_ListOffsetArray_rpad_and_clip_axis1_64<T>(
        outindex.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1,
        target);
      util::handle_error(err2, classname(), identities_.get());

      std::shared_ptr<IndexedOptionArray64> next =
        std::make_shared<IndexedOptionArray64>(Identities::none(),
                                               util::Parameters(),
                                               outindex,
                                               content());
      return std::make_shared<RegularArray>(Identities::none(),
                                            parameters_,
                                            next.get()->simplify_optiontype(),
                                            target);
    }
    else {
      return std::make_shared<ListOffsetArrayOf<T>>(
        Identities::none(), parameters_, offsets_,
        content_.get()->rpad_and_clip(target, toaxis, depth + 1));
    }
  }

  template <>
  const ContentPtr ListOffsetArrayOf<int64_t>::reduce_next(
    const Reducer& reducer,
    int64_t negaxis,
    const Index64& starts,
    const Index64& parents,
    int64_t outlength, bool mask, bool keepdims) const {

    std::pair<bool, int64_t> branchdepth = branch_depth();

    if (!branchdepth.first  &&  negaxis == branchdepth.second) {
      if (offsets_.length() - 1 != parents.length()) {
        throw std::runtime_error("offsets_.length() - 1 != parents.length()");
      }

      int64_t globalstart;
      int64_t globalstop;
      struct Error err1 = awkward_listoffsetarray_reduce_global_startstop_64(
        &globalstart,
        &globalstop,
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err1, classname(), identities_.get());
      int64_t nextlen = globalstop - globalstart;

      int64_t maxcount;
      Index64 offsetscopy(offsets_.length());
      struct Error err2 =
        awkward_listoffsetarray_reduce_nonlocal_maxcount_offsetscopy_64(
        &maxcount,
        offsetscopy.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err2, classname(), identities_.get());

      Index64 nextcarry(nextlen);
      Index64 nextparents(nextlen);
      int64_t maxnextparents;
      Index64 distincts(maxcount * outlength);
      struct Error err3 =
        awkward_listoffsetarray_reduce_nonlocal_preparenext_64(
        nextcarry.ptr().get(),
        nextparents.ptr().get(),
        nextlen,
        &maxnextparents,
        distincts.ptr().get(),
        maxcount * outlength,
        offsetscopy.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1,
        parents.ptr().get(),
        parents.offset(),
        maxcount);
      util::handle_error(err3, classname(), identities_.get());

      Index64 nextstarts(maxnextparents + 1);
      struct Error err4 =
        awkward_listoffsetarray_reduce_nonlocal_nextstarts_64(
        nextstarts.ptr().get(),
        nextparents.ptr().get(),
        nextlen);
      util::handle_error(err4, classname(), identities_.get());

      ContentPtr nextcontent = content_.get()->carry(nextcarry);
      ContentPtr outcontent = nextcontent.get()->reduce_next(
        reducer, negaxis - 1, nextstarts, nextparents, maxnextparents + 1,
        mask, false);

      Index64 gaps(outlength);
      struct Error err5 = awkward_listoffsetarray_reduce_nonlocal_findgaps_64(
        gaps.ptr().get(),
        parents.ptr().get(),
        parents.offset(),
        parents.length());
      util::handle_error(err5, classname(), identities_.get());

      Index64 outstarts(outlength);
      Index64 outstops(outlength);
      struct Error err6 =
        awkward_listoffsetarray_reduce_nonlocal_outstartsstops_64(
        outstarts.ptr().get(),
        outstops.ptr().get(),
        distincts.ptr().get(),
        maxcount * outlength,
        gaps.ptr().get(),
        outlength);
      util::handle_error(err6, classname(), identities_.get());

      ContentPtr out = std::make_shared<ListArray64>(Identities::none(),
                                                     util::Parameters(),
                                                     outstarts,
                                                     outstops,
                                                     outcontent);
      if (keepdims) {
        out = std::make_shared<RegularArray>(Identities::none(),
                                             util::Parameters(),
                                             out,
                                             1);
      }
      return out;
    }

    else {
      int64_t globalstart;
      int64_t globalstop;
      struct Error err1 = awkward_listoffsetarray_reduce_global_startstop_64(
        &globalstart,
        &globalstop,
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err1, classname(), identities_.get());

      Index64 nextparents(globalstop - globalstart);
      struct Error err2 = awkward_listoffsetarray_reduce_local_nextparents_64(
        nextparents.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err2, classname(), identities_.get());

      ContentPtr trimmed = content_.get()->getitem_range_nowrap(globalstart,
                                                                globalstop);
      ContentPtr outcontent = trimmed.get()->reduce_next(
        reducer, negaxis, util::make_starts(offsets_), nextparents,
        offsets_.length() - 1, mask, keepdims);

      Index64 outoffsets(outlength + 1);
      struct Error err3 = awkward_listoffsetarray_reduce_local_outoffsets_64(
        outoffsets.ptr().get(),
        parents.ptr().get(),
        parents.offset(),
        parents.length(),
        outlength);
      util::handle_error(err3, classname(), identities_.get());

      return std::make_shared<ListOffsetArray64>(Identities::none(),
                                                 util::Parameters(),
                                                 outoffsets,
                                                 outcontent);
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::reduce_next(const Reducer& reducer,
                                    int64_t negaxis,
                                    const Index64& starts,
                                    const Index64& parents,
                                    int64_t length,
                                    bool mask,
                                    bool keepdims) const {
    return toListOffsetArray64(true).get()->reduce_next(reducer,
                                                        negaxis,
                                                        starts,
                                                        parents,
                                                        length,
                                                        mask,
                                                        keepdims);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::localindex(int64_t axis, int64_t depth) const {
    int64_t toaxis = axis_wrap_if_negative(axis);
    if (axis == depth) {
      return localindex_axis0();
    }
    else if (axis == depth + 1) {
      Index64 offsets = compact_offsets64(true);
      int64_t innerlength = offsets.getitem_at_nowrap(offsets.length() - 1);
      Index64 localindex(innerlength);
      struct Error err = util::awkward_listarray_localindex_64(
        localindex.ptr().get(),
        offsets.ptr().get(),
        offsets.offset(),
        offsets.length() - 1);
      util::handle_error(err, classname(), identities_.get());
      return std::make_shared<ListOffsetArray64>(
        identities_,
        util::Parameters(),
        offsets,
        std::make_shared<NumpyArray>(localindex));
    }
    else {
      return std::make_shared<ListOffsetArrayOf<T>>(
        identities_,
        util::Parameters(),
        offsets_,
        content_.get()->localindex(axis, depth + 1));
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::combinations(int64_t n,
                                     bool replacement,
                                     const util::RecordLookupPtr& recordlookup,
                                     const util::Parameters& parameters,
                                     int64_t axis,
                                     int64_t depth) const {
    if (n < 1) {
      throw std::invalid_argument("in combinations, 'n' must be at least 1");
    }

    int64_t toaxis = axis_wrap_if_negative(axis);
    if (toaxis == depth) {
      return combinations_axis0(n, replacement, recordlookup, parameters);
    }

    else if (toaxis == depth + 1) {
      IndexOf<T> starts = util::make_starts(offsets_);
      IndexOf<T> stops = util::make_stops(offsets_);

      int64_t totallen;
      Index64 offsets(length() + 1);
      struct Error err1 = util::awkward_listarray_combinations_length_64<T>(
        &totallen,
        offsets.ptr().get(),
        n,
        replacement,
        starts.ptr().get(),
        starts.offset(),
        stops.ptr().get(),
        stops.offset(),
        length());
      util::handle_error(err1, classname(), identities_.get());

      std::vector<std::shared_ptr<int64_t>> tocarry;
      std::vector<int64_t*> tocarryraw;
      for (int64_t j = 0;  j < n;  j++) {
        std::shared_ptr<int64_t> ptr(new int64_t[(size_t)totallen],
                                     util::array_deleter<int64_t>());
        tocarry.push_back(ptr);
        tocarryraw.push_back(ptr.get());
      }
      struct Error err2 = util::awkward_listarray_combinations_64<T>(
        tocarryraw.data(),
        n,
        replacement,
        starts.ptr().get(),
        starts.offset(),
        stops.ptr().get(),
        stops.offset(),
        length());
      util::handle_error(err2, classname(), identities_.get());

      ContentPtrVec contents;
      for (auto ptr : tocarry) {
        contents.push_back(content_.get()->carry(Index64(ptr, 0, totallen)));
      }
      ContentPtr recordarray = std::make_shared<RecordArray>(
        Identities::none(), parameters, contents, recordlookup);

      return std::make_shared<ListOffsetArray64>(identities_,
                                                 util::Parameters(),
                                                 offsets,
                                                 recordarray);
    }

    else {
      ContentPtr compact = toListOffsetArray64(true);
      ListOffsetArray64* rawcompact =
        dynamic_cast<ListOffsetArray64*>(compact.get());
      ContentPtr next = rawcompact->content().get()->combinations(n,
                                                                  replacement,
                                                                  recordlookup,
                                                                  parameters,
                                                                  axis,
                                                                  depth + 1);
      return std::make_shared<ListOffsetArray64>(identities_,
                                                 util::Parameters(),
                                                 rawcompact->offsets(),
                                                 next);
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::sort_next(int64_t negaxis,
                                  const Index64& starts,
                                  const Index64& parents,
                                  int64_t outlength,
                                  bool ascending,
                                  bool stable,
                                  bool keepdims) const {
    return toListOffsetArray64(true).get()->sort_next(negaxis,
                                                      starts,
                                                      parents,
                                                      outlength,
                                                      ascending,
                                                      stable,
                                                      keepdims);
  }

  template <>
  const ContentPtr ListOffsetArrayOf<int64_t>::sort_next(
    int64_t negaxis,
    const Index64& starts,
    const Index64& parents,
    int64_t outlength,
    bool ascending,
    bool stable,
    bool keepdims) const {

    std::pair<bool, int64_t> branchdepth = branch_depth();

    if (!branchdepth.first  &&  negaxis == branchdepth.second) {
      if (offsets_.length() - 1 != parents.length()) {
        throw std::runtime_error("offsets_.length() - 1 != parents.length()");
      }
      int64_t globalstart;
      int64_t globalstop;
      struct Error err1 = awkward_listoffsetarray_reduce_global_startstop_64(
        &globalstart,
        &globalstop,
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err1, classname(), identities_.get());
      int64_t nextlen = globalstop - globalstart;

      int64_t maxcount;
      Index64 offsetscopy(offsets_.length());
      struct Error err2 = awkward_listoffsetarray_reduce_nonlocal_maxcount_offsetscopy_64(
        &maxcount,
        offsetscopy.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err2, classname(), identities_.get());

      Index64 nextcarry(nextlen);
      Index64 nextparents(nextlen);
      int64_t maxnextparents;
      Index64 distincts(maxcount * outlength);
      struct Error err3 = awkward_listoffsetarray_reduce_nonlocal_preparenext_64(
        nextcarry.ptr().get(),
        nextparents.ptr().get(),
        nextlen,
        &maxnextparents,
        distincts.ptr().get(),
        maxcount * outlength,
        offsetscopy.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1,
        parents.ptr().get(),
        parents.offset(),
        maxcount);
      util::handle_error(err3, classname(), identities_.get());

      Index64 nextstarts(maxnextparents + 1);
      struct Error err4 =
        awkward_listoffsetarray_reduce_nonlocal_nextstarts_64(
        nextstarts.ptr().get(),
        nextparents.ptr().get(),
        nextlen);
      util::handle_error(err4, classname(), identities_.get());

      ContentPtr nextcontent = content_.get()->carry(nextcarry);

      ContentPtr outcontent = nextcontent.get()->sort_next(
        negaxis - 1, nextstarts, nextparents, nextcontent.get()->length(),
        ascending, stable, false);

      Index64 outcarry(nextlen);
      struct Error err5 =
        awkward_listoffsetarray_local_preparenext_64(
        outcarry.ptr().get(),
        nextcarry.ptr().get(),
        nextlen);
      util::handle_error(err5, classname(), identities_.get());

      outcontent = outcontent.get()->carry(outcarry);

      ContentPtr out = std::make_shared<ListOffsetArray64>(Identities::none(),
                                                           util::Parameters(),
                                                           offsets_,
                                                           outcontent);
      if (keepdims) {
        out = std::make_shared<RegularArray>(Identities::none(),
                                             util::Parameters(),
                                             out,
                                             out.get()->length());
      }
      return out;
    }
    else {
      int64_t globalstart;
      int64_t globalstop;
      struct Error err1 = awkward_listoffsetarray_reduce_global_startstop_64(
        &globalstart,
        &globalstop,
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err1, classname(), identities_.get());

      Index64 nextparents(globalstop - globalstart);
      struct Error err2 = awkward_listoffsetarray_reduce_local_nextparents_64(
        nextparents.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err2, classname(), identities_.get());

      ContentPtr trimmed = content_.get()->getitem_range_nowrap(globalstart,
                                                                globalstop);

      ContentPtr outcontent = trimmed.get()->sort_next(
        negaxis, util::make_starts(offsets_), nextparents,
        offsets_.length() - 1, ascending, stable, false);

      ContentPtr out = std::make_shared<ListOffsetArray64>(Identities::none(),
                                                 util::Parameters(),
                                                 offsets_,
                                                 outcontent);
      if (keepdims) {
        out = std::make_shared<RegularArray>(Identities::none(),
                                             util::Parameters(),
                                             out,
                                             out.get()->length());
      }
      return out;
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::argsort_next(int64_t negaxis,
                                     const Index64& starts,
                                     const Index64& parents,
                                     int64_t outlength,
                                     bool ascending,
                                     bool stable,
                                     bool keepdims) const {
    return toListOffsetArray64(true).get()->argsort_next(negaxis,
                                                         starts,
                                                         parents,
                                                         outlength,
                                                         ascending,
                                                         stable,
                                                         keepdims);
  }

  template <>
  const ContentPtr
  ListOffsetArrayOf<int64_t>::argsort_next(int64_t negaxis,
                                           const Index64& starts,
                                           const Index64& parents,
                                           int64_t outlength,
                                           bool ascending,
                                           bool stable,
                                           bool keepdims) const {
    std::pair<bool, int64_t> branchdepth = branch_depth();

    if (!branchdepth.first  &&  negaxis == branchdepth.second) {
      if (offsets_.length() - 1 != parents.length()) {
        throw std::runtime_error("offsets_.length() - 1 != parents.length()");
      }

      int64_t globalstart;
      int64_t globalstop;
      struct Error err1 = awkward_listoffsetarray_reduce_global_startstop_64(
        &globalstart,
        &globalstop,
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err1, classname(), identities_.get());
      int64_t nextlen = globalstop - globalstart;

      int64_t maxcount;
      Index64 offsetscopy(offsets_.length());
      struct Error err2 = awkward_listoffsetarray_reduce_nonlocal_maxcount_offsetscopy_64(
        &maxcount,
        offsetscopy.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err2, classname(), identities_.get());

      Index64 nextcarry(nextlen);
      Index64 nextparents(nextlen);
      int64_t maxnextparents;
      Index64 distincts(maxcount * outlength);
      struct Error err3 = awkward_listoffsetarray_reduce_nonlocal_preparenext_64(
        nextcarry.ptr().get(),
        nextparents.ptr().get(),
        nextlen,
        &maxnextparents,
        distincts.ptr().get(),
        maxcount * outlength,
        offsetscopy.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1,
        parents.ptr().get(),
        parents.offset(),
        maxcount);
      util::handle_error(err3, classname(), identities_.get());

      Index64 nextstarts(maxnextparents + 1);
      struct Error err4 = awkward_listoffsetarray_reduce_nonlocal_nextstarts_64(
        nextstarts.ptr().get(),
        nextparents.ptr().get(),
        nextlen);
      util::handle_error(err4, classname(), identities_.get());

      ContentPtr nextcontent = content_.get()->carry(nextcarry);

      ContentPtr outcontent = nextcontent.get()->argsort_next(
        negaxis - 1, nextstarts, nextparents, maxnextparents + 1,
        ascending, stable, false);

      Index64 outcarry(nextlen);
      struct Error err5 =
        awkward_listoffsetarray_local_preparenext_64(
        outcarry.ptr().get(),
        nextcarry.ptr().get(),
        nextlen);
      util::handle_error(err5, classname(), identities_.get());

      outcontent = outcontent.get()->carry(outcarry);

      ContentPtr out = std::make_shared<ListOffsetArray64>(Identities::none(),
                                                           util::Parameters(),
                                                           offsets_,
                                                           outcontent);
      if (keepdims) {
        out = std::make_shared<RegularArray>(Identities::none(),
                                             util::Parameters(),
                                             out,
                                             out.get()->length());
      }
      return out;
    }
    else {
      int64_t globalstart;
      int64_t globalstop;
      struct Error err1 = awkward_listoffsetarray_reduce_global_startstop_64(
        &globalstart,
        &globalstop,
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err1, classname(), identities_.get());

      Index64 nextparents(globalstop - globalstart);
      struct Error err2 = awkward_listoffsetarray_reduce_local_nextparents_64(
        nextparents.ptr().get(),
        offsets_.ptr().get(),
        offsets_.offset(),
        offsets_.length() - 1);
      util::handle_error(err2, classname(), identities_.get());

      ContentPtr trimmed = content_.get()->getitem_range_nowrap(globalstart,
                                                                globalstop);

      ContentPtr outcontent = trimmed.get()->argsort_next(
        negaxis, util::make_starts(offsets_), nextparents,
        offsets_.length() - 1, ascending, stable, false);

      ContentPtr out = std::make_shared<ListOffsetArray64>(Identities::none(),
                                                 util::Parameters(),
                                                 offsets_,
                                                 outcontent);
      if (keepdims) {
        out = std::make_shared<RegularArray>(Identities::none(),
                                             util::Parameters(),
                                             out,
                                             out.get()->length());
      }
      return out;
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next(const SliceAt& at,
                                     const Slice& tail,
                                     const Index64& advanced) const {
    if (advanced.length() != 0) {
      throw std::runtime_error(
        "ListOffsetArray::getitem_next(SliceAt): advanced.length() != 0");
    }
    int64_t lenstarts = offsets_.length() - 1;
    IndexOf<T> starts = util::make_starts(offsets_);
    IndexOf<T> stops = util::make_stops(offsets_);
    SliceItemPtr nexthead = tail.head();
    Slice nexttail = tail.tail();
    Index64 nextcarry(lenstarts);
    struct Error err = util::awkward_listarray_getitem_next_at_64<T>(
      nextcarry.ptr().get(),
      starts.ptr().get(),
      stops.ptr().get(),
      lenstarts,
      starts.offset(),
      stops.offset(),
      at.at());
    util::handle_error(err, classname(), identities_.get());
    ContentPtr nextcontent = content_.get()->carry(nextcarry);
    return nextcontent.get()->getitem_next(nexthead, nexttail, advanced);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next(const SliceRange& range,
                                     const Slice& tail,
                                     const Index64& advanced) const {
    int64_t lenstarts = offsets_.length() - 1;
    IndexOf<T> starts = util::make_starts(offsets_);
    IndexOf<T> stops = util::make_stops(offsets_);
    SliceItemPtr nexthead = tail.head();
    Slice nexttail = tail.tail();
    int64_t start = range.start();
    int64_t stop = range.stop();
    int64_t step = range.step();
    if (step == Slice::none()) {
      step = 1;
    }
    int64_t carrylength;
    struct Error err1 =
      util::awkward_listarray_getitem_next_range_carrylength<T>(
      &carrylength,
      starts.ptr().get(),
      stops.ptr().get(),
      lenstarts,
      starts.offset(),
      stops.offset(),
      start,
      stop,
      step);
    util::handle_error(err1, classname(), identities_.get());

    IndexOf<T> nextoffsets(lenstarts + 1);
    Index64 nextcarry(carrylength);

    struct Error err2 = util::awkward_listarray_getitem_next_range_64<T>(
      nextoffsets.ptr().get(),
      nextcarry.ptr().get(),
      starts.ptr().get(),
      stops.ptr().get(),
      lenstarts,
      starts.offset(),
      stops.offset(),
      start,
      stop,
      step);
    util::handle_error(err2, classname(), identities_.get());
    ContentPtr nextcontent = content_.get()->carry(nextcarry);

    if (advanced.length() == 0) {
      return std::make_shared<ListOffsetArrayOf<T>>(
        identities_,
        parameters_,
        nextoffsets,
        nextcontent.get()->getitem_next(nexthead, nexttail, advanced));
    }
    else {
      int64_t total;
      struct Error err1 =
        util::awkward_listarray_getitem_next_range_counts_64<T>(
        &total,
        nextoffsets.ptr().get(),
        lenstarts);
      util::handle_error(err1, classname(), identities_.get());
      Index64 nextadvanced(total);
      struct Error err2 =
        util::awkward_listarray_getitem_next_range_spreadadvanced_64<T>(
        nextadvanced.ptr().get(),
        advanced.ptr().get(),
        nextoffsets.ptr().get(),
        lenstarts);
      util::handle_error(err2, classname(), identities_.get());
      return std::make_shared<ListOffsetArrayOf<T>>(
        identities_,
        parameters_,
        nextoffsets,
        nextcontent.get()->getitem_next(nexthead, nexttail, nextadvanced));
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next(const SliceArray64& array,
                                     const Slice& tail,
                                     const Index64& advanced) const {
    int64_t lenstarts = offsets_.length() - 1;
    IndexOf<T> starts = util::make_starts(offsets_);
    IndexOf<T> stops = util::make_stops(offsets_);
    SliceItemPtr nexthead = tail.head();
    Slice nexttail = tail.tail();
    Index64 flathead = array.ravel();
    if (advanced.length() == 0) {
      Index64 nextcarry(lenstarts*flathead.length());
      Index64 nextadvanced(lenstarts*flathead.length());
      struct Error err = util::awkward_listarray_getitem_next_array_64<T>(
        nextcarry.ptr().get(),
        nextadvanced.ptr().get(),
        starts.ptr().get(),
        stops.ptr().get(),
        flathead.ptr().get(),
        starts.offset(),
        stops.offset(),
        lenstarts,
        flathead.length(),
        content_.get()->length());
      util::handle_error(err, classname(), identities_.get());
      ContentPtr nextcontent = content_.get()->carry(nextcarry);
      return getitem_next_array_wrap(
               nextcontent.get()->getitem_next(nexthead,
                                               nexttail,
                                               nextadvanced),
               array.shape());
    }
    else {
      Index64 nextcarry(lenstarts);
      Index64 nextadvanced(lenstarts);
      struct Error err =
        util::awkward_listarray_getitem_next_array_advanced_64<T>(
        nextcarry.ptr().get(),
        nextadvanced.ptr().get(),
        starts.ptr().get(),
        stops.ptr().get(),
        flathead.ptr().get(),
        advanced.ptr().get(),
        starts.offset(),
        stops.offset(),
        lenstarts,
        flathead.length(),
        content_.get()->length());
      util::handle_error(err, classname(), identities_.get());
      ContentPtr nextcontent = content_.get()->carry(nextcarry);
      return nextcontent.get()->getitem_next(nexthead, nexttail, nextadvanced);
    }
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next(const SliceJagged64& jagged,
                                     const Slice& tail,
                                     const Index64& advanced) const {
    ListArrayOf<T> listarray(identities_,
                             parameters_,
                             util::make_starts(offsets_),
                             util::make_stops(offsets_),
                             content_);
    return listarray.getitem_next(jagged, tail, advanced);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next_jagged(const Index64& slicestarts,
                                            const Index64& slicestops,
                                            const SliceArray64& slicecontent,
                                            const Slice& tail) const {
    ListArrayOf<T> listarray(identities_,
                             parameters_,
                             util::make_starts(offsets_),
                             util::make_stops(offsets_),
                             content_);
    return listarray.getitem_next_jagged(slicestarts,
                                         slicestops,
                                         slicecontent,
                                         tail);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next_jagged(const Index64& slicestarts,
                                            const Index64& slicestops,
                                            const SliceMissing64& slicecontent,
                                            const Slice& tail) const {
    ListArrayOf<T> listarray(identities_,
                             parameters_,
                             util::make_starts(offsets_),
                             util::make_stops(offsets_),
                             content_);
    return listarray.getitem_next_jagged(slicestarts,
                                         slicestops,
                                         slicecontent,
                                         tail);
  }

  template <typename T>
  const ContentPtr
  ListOffsetArrayOf<T>::getitem_next_jagged(const Index64& slicestarts,
                                            const Index64& slicestops,
                                            const SliceJagged64& slicecontent,
                                            const Slice& tail) const {
    ListArrayOf<T> listarray(identities_,
                             parameters_,
                             util::make_starts(offsets_),
                             util::make_stops(offsets_),
                             content_);
    return listarray.getitem_next_jagged(slicestarts,
                                         slicestops,
                                         slicecontent,
                                         tail);
  }

  template class ListOffsetArrayOf<int32_t>;
  template class ListOffsetArrayOf<uint32_t>;
  template class ListOffsetArrayOf<int64_t>;
}
