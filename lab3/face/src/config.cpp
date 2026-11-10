#include "config.hpp"

#include <opencv2/core/core.hpp>
#include <iostream>

// 檔案裡沒寫的欄位維持預設值
template <typename T>
static void read_field ( const cv::FileStorage &fs, const char *name, T &value )
{
    cv::FileNode node = fs[name];
    if ( !node.empty ( ) ) node >> value;
}

bool Config::load ( const std::string &path )
{
    cv::FileStorage fs ( path, cv::FileStorage::READ );
    if ( !fs.isOpened ( ) )
    {
        std::cerr << "Could not open config " << path << std::endl;
        return false;
    }
    read_field ( fs, "camera_index", camera_index );
    read_field ( fs, "camera_width", camera_width );
    read_field ( fs, "camera_height", camera_height );
    read_field ( fs, "fb_device", fb_device );
    read_field ( fs, "display_fit", display_fit );

    read_field ( fs, "face_cascade", face_cascade );
    read_field ( fs, "eye_cascade", eye_cascade );
    read_field ( fs, "detect_width", detect_width );
    read_field ( fs, "scale_factor", scale_factor );
    read_field ( fs, "min_neighbors", min_neighbors );
    read_field ( fs, "min_face", min_face );
    read_field ( fs, "overlap_merge", overlap_merge );

    read_field ( fs, "model_param", model_param );
    read_field ( fs, "model_bin", model_bin );
    read_field ( fs, "input_blob", input_blob );
    read_field ( fs, "output_blob", output_blob );
    read_field ( fs, "input_rgb", input_rgb );
    read_field ( fs, "ncnn_threads", ncnn_threads );

    read_field ( fs, "gallery", gallery );
    read_field ( fs, "sim_threshold", sim_threshold );
    read_field ( fs, "sim_margin", sim_margin );

    read_field ( fs, "recog_interval", recog_interval );
    read_field ( fs, "vote_window", vote_window );
    read_field ( fs, "vote_min", vote_min );
    read_field ( fs, "iou_match", iou_match );
    read_field ( fs, "center_match", center_match );
    read_field ( fs, "ema_alpha", ema_alpha );
    read_field ( fs, "snap_distance", snap_distance );
    read_field ( fs, "min_hits", min_hits );
    read_field ( fs, "track_timeout", track_timeout );
    return true;
}
