-- ESP32 溫度資料收集系統：資料表與 TimescaleDB 設定
-- 在 psql 或 pgAdmin 的 Query Tool 執行一次即可

CREATE EXTENSION IF NOT EXISTS timescaledb;

-- 感測資料表（api_server.py、mqtt_to_timescale.py 與 ESP32 直連版本共用）
CREATE TABLE IF NOT EXISTS sensor_data (
    time        TIMESTAMPTZ      NOT NULL,
    device_id   TEXT             NOT NULL,
    temperature DOUBLE PRECISION,
    humidity    DOUBLE PRECISION
);

-- 轉成 hypertable：依時間自動分區（chunk），大量時序資料寫入和查詢都比較快
SELECT create_hypertable('sensor_data', 'time', if_not_exists => TRUE);

-- 查詢單一設備最新資料用的索引
CREATE INDEX IF NOT EXISTS idx_sensor_device_time
    ON sensor_data (device_id, time DESC);

-- 壓縮：依 device_id 分組，超過 7 天的資料自動壓縮
-- （這是選用 TimescaleDB 的主要原因：舊資料壓縮後可大幅減少硬碟空間）
ALTER TABLE sensor_data SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id'
);
SELECT add_compression_policy('sensor_data', INTERVAL '7 days', if_not_exists => TRUE);

-- （選用）保留策略：超過 90 天的資料自動刪除
-- SELECT add_retention_policy('sensor_data', INTERVAL '90 days', if_not_exists => TRUE);

-- 查看壓縮效果
-- SELECT * FROM hypertable_compression_stats('sensor_data');
