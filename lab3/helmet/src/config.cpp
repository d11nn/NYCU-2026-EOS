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
    read_field ( fs, "model_param", model_param );
    read_field ( fs, "model_bin", model_bin );
    read_field ( fs, "input_blob", input_blob );
    read_field ( fs, "output_blob", output_blob );
    read_field ( fs, "input_size", input_size );
    read_field ( fs, "ncnn_threads", ncnn_threads );

    read_field ( fs, "conf_threshold", conf_threshold );
    read_field ( fs, "nms_iou", nms_iou );
    read_field ( fs, "contain", contain );
    read_field ( fs, "tile_grid", tile_grid );
    read_field ( fs, "tile_overlap", tile_overlap );
    read_field ( fs, "tile_max_box", tile_max_box );

    read_field ( fs, "fb_device", fb_device );
    read_field ( fs, "jpeg_quality", jpeg_quality );
    read_field ( fs, "wait_enter", wait_enter );
    return true;
}
