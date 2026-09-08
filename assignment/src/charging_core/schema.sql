CREATE SCHEMA IF NOT EXISTS {schema};
SET search_path TO {schema}, public;

CREATE TABLE IF NOT EXISTS app_user (
    id uuid PRIMARY KEY,
    phone varchar(11) NOT NULL UNIQUE CHECK (phone ~ '^1[0-9]{{10}}$'),
    nickname varchar(20) NOT NULL CHECK (length(btrim(nickname)) BETWEEN 1 AND 20),
    avatar_path varchar(300),
    balance numeric(12, 2) NOT NULL DEFAULT 0 CHECK (balance >= 0),
    status varchar(16) NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'frozen')),
    created_at timestamptz NOT NULL DEFAULT now()
);
ALTER TABLE app_user ADD COLUMN IF NOT EXISTS avatar_path varchar(300);

CREATE TABLE IF NOT EXISTS otp_challenge (
    phone varchar(11) PRIMARY KEY CHECK (phone ~ '^1[0-9]{{10}}$'),
    code_digest char(64) NOT NULL,
    expires_at timestamptz NOT NULL,
    attempts smallint NOT NULL DEFAULT 0 CHECK (attempts BETWEEN 0 AND 5),
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS admin_account (
    id uuid PRIMARY KEY,
    username varchar(64) NOT NULL UNIQUE,
    password_hash text NOT NULL,
    active boolean NOT NULL DEFAULT true,
    failed_attempts smallint NOT NULL DEFAULT 0 CHECK (failed_attempts BETWEEN 0 AND 5),
    locked_until timestamptz,
    created_at timestamptz NOT NULL DEFAULT now()
);
ALTER TABLE admin_account
    ADD COLUMN IF NOT EXISTS failed_attempts smallint NOT NULL DEFAULT 0
    CHECK (failed_attempts BETWEEN 0 AND 5);
ALTER TABLE admin_account ADD COLUMN IF NOT EXISTS locked_until timestamptz;

CREATE TABLE IF NOT EXISTS station (
    id uuid PRIMARY KEY,
    name varchar(100) NOT NULL,
    address varchar(200) NOT NULL,
    longitude numeric(9, 6) NOT NULL CHECK (longitude BETWEEN -180 AND 180),
    latitude numeric(8, 6) NOT NULL CHECK (latitude BETWEEN -90 AND 90),
    unit_price numeric(8, 2) NOT NULL CHECK (unit_price > 0),
    source_dataset varchar(40),
    source_station_id varchar(80),
    zone_id integer,
    created_at timestamptz NOT NULL DEFAULT now()
);
ALTER TABLE station ADD COLUMN IF NOT EXISTS source_dataset varchar(40);
ALTER TABLE station ADD COLUMN IF NOT EXISTS source_station_id varchar(80);
ALTER TABLE station ADD COLUMN IF NOT EXISTS zone_id integer;
CREATE UNIQUE INDEX IF NOT EXISTS uq_station_dataset_source
    ON station(source_dataset, source_station_id)
    WHERE source_dataset IS NOT NULL AND source_station_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS charger (
    id uuid PRIMARY KEY,
    station_id uuid NOT NULL REFERENCES station(id) ON DELETE RESTRICT,
    code varchar(40) NOT NULL UNIQUE,
    kind varchar(16) NOT NULL CHECK (kind IN ('fast', 'slow')),
    power_kw numeric(8, 2) NOT NULL CHECK (power_kw > 0),
    status varchar(16) NOT NULL DEFAULT 'available'
        CHECK (status IN ('available', 'reserved', 'charging', 'faulted')),
    total_sessions integer NOT NULL DEFAULT 0 CHECK (total_sessions >= 0),
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS charging_order (
    id uuid PRIMARY KEY,
    user_id uuid NOT NULL REFERENCES app_user(id) ON DELETE RESTRICT,
    charger_id uuid NOT NULL REFERENCES charger(id) ON DELETE RESTRICT,
    request_key uuid NOT NULL,
    status varchar(16) NOT NULL CHECK (status IN ('reserved', 'charging', 'completed', 'cancelled')),
    unit_price numeric(8, 2) NOT NULL CHECK (unit_price > 0),
    time_scale integer NOT NULL CHECK (time_scale BETWEEN 1 AND 3600),
    reserved_at timestamptz NOT NULL DEFAULT now(),
    reserved_until timestamptz NOT NULL,
    started_at timestamptz,
    ended_at timestamptz,
    energy_kwh numeric(14, 6) NOT NULL DEFAULT 0 CHECK (energy_kwh >= 0),
    amount numeric(12, 2) NOT NULL DEFAULT 0 CHECK (amount >= 0),
    stop_reason varchar(32),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (user_id, request_key),
    CHECK (
        (status = 'reserved' AND started_at IS NULL AND ended_at IS NULL)
        OR (status = 'charging' AND started_at IS NOT NULL AND ended_at IS NULL)
        OR (status IN ('completed', 'cancelled') AND ended_at IS NOT NULL)
    )
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_active_order_per_user
    ON charging_order(user_id) WHERE status IN ('reserved', 'charging');
CREATE UNIQUE INDEX IF NOT EXISTS uq_active_order_per_charger
    ON charging_order(charger_id) WHERE status IN ('reserved', 'charging');
CREATE INDEX IF NOT EXISTS ix_order_user_created
    ON charging_order(user_id, created_at DESC);

CREATE TABLE IF NOT EXISTS wallet_entry (
    id uuid PRIMARY KEY,
    user_id uuid NOT NULL REFERENCES app_user(id) ON DELETE RESTRICT,
    idempotency_key uuid NOT NULL,
    entry_type varchar(16) NOT NULL CHECK (entry_type IN ('recharge', 'charge')),
    amount numeric(12, 2) NOT NULL CHECK (amount <> 0),
    balance_after numeric(12, 2) NOT NULL CHECK (balance_after >= 0),
    order_id uuid REFERENCES charging_order(id) ON DELETE RESTRICT,
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (user_id, idempotency_key)
);

CREATE TABLE IF NOT EXISTS domain_event (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    audience_user_id uuid REFERENCES app_user(id) ON DELETE CASCADE,
    audience_role varchar(16) CHECK (audience_role IN ('user', 'admin')),
    event_type varchar(80) NOT NULL,
    payload jsonb NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);
ALTER TABLE domain_event
    ADD COLUMN IF NOT EXISTS audience_role varchar(16)
    CHECK (audience_role IN ('user', 'admin'));
CREATE INDEX IF NOT EXISTS ix_event_audience_cursor
    ON domain_event(audience_user_id, id);
CREATE INDEX IF NOT EXISTS ix_event_role_cursor
    ON domain_event(audience_role, id);

CREATE TABLE IF NOT EXISTS ops_log (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    admin_id uuid NOT NULL REFERENCES admin_account(id) ON DELETE RESTRICT,
    action varchar(80) NOT NULL,
    target_type varchar(40) NOT NULL,
    target_id uuid,
    detail jsonb NOT NULL DEFAULT '{{}}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS ix_ops_log_created ON ops_log(created_at DESC);

CREATE TABLE IF NOT EXISTS dataset_metadata (
    name varchar(80) PRIMARY KEY,
    metadata jsonb NOT NULL,
    imported_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS urbanev_hourly_profile (
    hour smallint PRIMARY KEY CHECK (hour BETWEEN 0 AND 23),
    occupancy_rate numeric(6, 2) NOT NULL CHECK (occupancy_rate BETWEEN 0 AND 100),
    energy_kwh numeric(14, 2) NOT NULL CHECK (energy_kwh >= 0),
    electricity_price numeric(8, 3) NOT NULL CHECK (electricity_price >= 0),
    service_price numeric(8, 3) NOT NULL CHECK (service_price >= 0)
);

CREATE TABLE IF NOT EXISTS schema_version (
    version integer PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT now()
);
INSERT INTO schema_version(version) VALUES (2) ON CONFLICT DO NOTHING;
INSERT INTO schema_version(version) VALUES (3) ON CONFLICT DO NOTHING;

-- Historical research observations remain separate from operational billing.
CREATE TABLE IF NOT EXISTS urbanev_source_file (
    path text PRIMARY KEY, source_commit text NOT NULL, sha256 text NOT NULL,
    byte_count bigint NOT NULL, content bytea NOT NULL
);
CREATE TABLE IF NOT EXISTS urbanev_station (
    source_id integer PRIMARY KEY, zone_id integer NOT NULL,
    longitude_gcj02 double precision NOT NULL, latitude_gcj02 double precision NOT NULL,
    capacity integer NOT NULL
);
CREATE TABLE IF NOT EXISTS urbanev_observation (
    zone_id integer NOT NULL, observed_at timestamp NOT NULL,
    occupied double precision, duration_hours double precision,
    energy_kwh double precision, energy_11kw double precision,
    electricity_price double precision, service_price double precision,
    PRIMARY KEY(zone_id, observed_at)
);

-- Console settings and auditable administrator money operations (migration 4).
ALTER TABLE wallet_entry DROP CONSTRAINT IF EXISTS wallet_entry_entry_type_check;
ALTER TABLE wallet_entry ADD CONSTRAINT wallet_entry_entry_type_check CHECK (entry_type IN ('recharge','charge','adjustment','refund'));
CREATE TABLE IF NOT EXISTS console_settings (id smallint PRIMARY KEY CHECK(id=1),value jsonb NOT NULL);
INSERT INTO schema_version(version) VALUES(4) ON CONFLICT DO NOTHING;
