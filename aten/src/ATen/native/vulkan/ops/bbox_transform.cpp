// Copyright 2004-present Facebook. All Rights Reserved.

#include <ATen/native/vulkan/ops/bbox_transform.h>

namespace at {
namespace native {

float to_float(double f) {return (float)f;};

std::tuple<at::Tensor, at::Tensor> BBoxTransformCPUKernel(
    const at::Tensor& roi_in_,
    const at::Tensor& delta_in_,
    const at::Tensor& iminfo_in_,
    c10::ArrayRef<double> weights_,
    bool apply_scale_,
    bool rotated_,
    bool angle_bound_on_,
    int64_t angle_bound_lo_,
    int64_t angle_bound_hi_,
    double clip_angle_thresh_,
    bool legacy_plus_one_,
    c10::optional<std::vector<torch::Tensor>>
  ) {
  const auto roi_in = roi_in_.contiguous();
  const auto delta_in = delta_in_.contiguous();
  const auto iminfo_in = iminfo_in_.contiguous();

  const int box_dim = rotated_ ? 5 : 4;
  const int N = roi_in.size(0);
  TORCH_CHECK(roi_in.dim() == 2);
  TORCH_CHECK(roi_in.size(1) == box_dim || roi_in.size(1) == box_dim + 1);

  TORCH_CHECK(delta_in.dim() == 2);
  TORCH_CHECK(delta_in.size(0) == N);
  TORCH_CHECK(delta_in.size(1) % box_dim == 0);
  const int num_classes = delta_in.size(1) / box_dim;

  TORCH_CHECK(iminfo_in.dim() == 2);
  TORCH_CHECK(iminfo_in.size(1) == 3);
  const int batch_size = iminfo_in.size(0);

  TORCH_CHECK(weights_.size() == 4);
  std::vector<float> float_weights(weights_.size(), 0);
  std::transform(weights_.begin(), weights_.end(), float_weights.begin(), to_float);

  Eigen::Map<const caffe2::ERArrXXf> boxes0(
      roi_in.data_ptr<float>(), roi_in.size(0), roi_in.size(1));
  Eigen::Map<const caffe2::ERArrXXf> deltas0(
      delta_in.data_ptr<float>(), delta_in.size(0), delta_in.size(1));

  // Count the number of RoIs per batch
  std::vector<int> num_rois_per_batch(batch_size, 0);
  if (roi_in.size(1) == box_dim) {
    TORCH_CHECK(batch_size == 1);
    num_rois_per_batch[0] = N;
  } else {
    const auto& roi_batch_ids = boxes0.col(0);
    for (int i = 0; i < roi_batch_ids.size(); ++i) {
      const int roi_batch_id = roi_batch_ids(i);
      TORCH_CHECK(roi_batch_id < batch_size);
      num_rois_per_batch[roi_batch_id]++;
    }
  }

  TORCH_CHECK(iminfo_in.sizes() == (at::IntArrayRef{batch_size, 3}));
  Eigen::Map<const caffe2::ERArrXXf> iminfo(
      iminfo_in.data_ptr<float>(), iminfo_in.size(0), iminfo_in.size(1));

  auto box_out = torch::zeros(delta_in.sizes(), roi_in.options());
  Eigen::Map<caffe2::ERArrXXf> new_boxes(
      box_out.data_ptr<float>(),
      box_out.size(0),
      box_out.size(1));

  // We assume roi_in and delta_in over multiple batches are grouped
  // together in increasing order as generated by GenerateProposalsOp
  int offset = 0;
  for (int i = 0; i < batch_size; ++i) {
    const int num_rois = num_rois_per_batch[i];
    const auto& cur_iminfo = iminfo.row(i);
    const float scale_before = cur_iminfo(2);
    const float scale_after = apply_scale_ ? cur_iminfo(2) : 1.0;
    int img_h = int(cur_iminfo(0) / scale_before + 0.5);
    int img_w = int(cur_iminfo(1) / scale_before + 0.5);

    caffe2::EArrXXf cur_boxes =
        boxes0.rightCols(box_dim).block(offset, 0, num_rois, box_dim);
    // Do not apply scale for angle in rotated boxes
    cur_boxes.leftCols(4) /= scale_before;
    for (int k = 0; k < num_classes; k++) {
      const auto& cur_deltas =
          deltas0.block(offset, k * box_dim, num_rois, box_dim);
      const auto& trans_boxes = caffe2::utils::bbox_transform(
          cur_boxes,
          cur_deltas,
          float_weights,
          caffe2::utils::BBOX_XFORM_CLIP_DEFAULT,
          legacy_plus_one_,
          angle_bound_on_,
          (int)angle_bound_lo_,
          (int)angle_bound_hi_);
      caffe2::EArrXXf clip_boxes = caffe2::utils::clip_boxes(
          trans_boxes, img_h, img_w, clip_angle_thresh_, legacy_plus_one_);
      // Do not apply scale for angle in rotated boxes
      clip_boxes.leftCols(4) *= scale_after;
      new_boxes.block(offset, k * box_dim, num_rois, box_dim) = clip_boxes;
    }

    offset += num_rois;
  }

  auto roi_batch_splits = torch::zeros({batch_size}, roi_in.options());
  Eigen::Map<caffe2::EArrXf> roi_batch_splits_map(roi_batch_splits.data_ptr<float>(), batch_size);
  roi_batch_splits_map =
      Eigen::Map<const caffe2::EArrXi>(num_rois_per_batch.data(), batch_size)
          .cast<float>();

  return std::make_tuple(box_out, roi_batch_splits);
}

std::tuple<at::Tensor, at::Tensor> BBoxTransformVulkanKernel(
    const at::Tensor& roi_in_,
    const at::Tensor& delta_in_,
    const at::Tensor& iminfo_in_,
    c10::ArrayRef<double> weights_,
    bool apply_scale_,
    bool rotated_,
    bool angle_bound_on_,
    int64_t angle_bound_lo_,
    int64_t angle_bound_hi_,
    double clip_angle_thresh_,
    bool legacy_plus_one_,
    c10::optional<std::vector<torch::Tensor>>
  ) {
  return BBoxTransformCPUKernel(
    roi_in_.cpu(),
    delta_in_.cpu(),
    iminfo_in_.cpu(),
    weights_,
    apply_scale_,
    rotated_,
    angle_bound_on_,
    angle_bound_lo_,
    angle_bound_hi_,
    clip_angle_thresh_,
    legacy_plus_one_,
    c10::nullopt
  );
}

}
}
