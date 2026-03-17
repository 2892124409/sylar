CREATE TABLE IF NOT EXISTS conversations (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    sid VARCHAR(128) NOT NULL,
    conversation_id VARCHAR(128) NOT NULL,
    created_at_ms BIGINT UNSIGNED NOT NULL,
    updated_at_ms BIGINT UNSIGNED NOT NULL,
    summary_text MEDIUMTEXT NOT NULL DEFAULT '',
    summary_updated_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    UNIQUE KEY uniq_sid_conv (sid, conversation_id),
    KEY idx_updated_at_ms (updated_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS chat_messages (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    sid VARCHAR(128) NOT NULL,
    conversation_id VARCHAR(128) NOT NULL,
    role VARCHAR(16) NOT NULL,
    content MEDIUMTEXT NOT NULL,
    created_at_ms BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (id),
    KEY idx_sid_conv_time (sid, conversation_id, created_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

ALTER TABLE conversations
    ADD COLUMN IF NOT EXISTS summary_text MEDIUMTEXT NOT NULL DEFAULT '';

ALTER TABLE conversations
    ADD COLUMN IF NOT EXISTS summary_updated_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0;
