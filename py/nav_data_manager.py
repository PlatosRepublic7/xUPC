import sqlite3
from tqdm import tqdm

APT_DAT_PATH = 'C:/X-Plane 12/Global Scenery/Global Airports/Earth nav data/apt.dat'


class LookaheadIterator:
    """
    Wrapper class that takes a traditional iterator object and allows for 'putting back' a single
    line for later access.
    """
    def __init__(self, iterator):
        self.iterator = iterator
        self.buffered_line = None

    
    def get_next_line(self):
        """
        Gets the next line, either from the buffer if a line was 'put back', or from
        the main file iterator.
        """
        if self.buffered_line is not None:
            # If a line is in our buffer, return it and clear the buffer
            line_to_return = self.buffered_line
            self.buffered_line = None
            return line_to_return
        else:
            return next(self.iterator)
        
    
    def put_line_back(self, line):
        """
        'Puts a line back' by storing it in the buffer.
        This assumes we only ever need to put back one line at a time.
        """
        if self.buffered_line is not None:
            raise RuntimeError("Cannot put back more than one line at a time.")
        self.buffered_line = line


class NavData:
    """
    Manages the navigation database, including parsing the apt.dat file and providing methods
    to query the stored data.
    """
    def __init__(self, db_path):
        """
        Initializes the NavDataManager with the path to the SQLite database.
        """
        self.db_path = db_path
        self.conn = None
        self.cursor = None


    def __enter__(self):
        """Opens the database connection when entering a 'with' block."""
        self.conn = sqlite3.connect(self.db_path)
        # Use row factory to access columns by name
        self.conn.row_factory = sqlite3.Row
        self.cursor = self.conn.cursor()
        return self
    

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Closes the connection when exiting a 'with' block."""
        if self.conn:
            self.conn.close()

    
    def _create_tables(self):
        """A private method to create the database schema"""
        # Contains all the CREATE TABLE IF NOT EXISTS statements
        self.cursor.execute("""
            CREATE TABLE IF NOT EXISTS airports (
                icao TEXT PRIMARY KEY,
                iata TEXT,
                faa TEXT,
                airport_name TEXT,
                elevation INT,
                type TEXT,
                latitude REAL,
                longitude REAL,
                country TEXT,
                city TEXT,
                region TEXT,
                transition_alt INT,
                transition_level INT               
            )""")
        
        self.cursor.execute("""
            CREATE TABLE IF NOT EXISTS runways (
                runway_id INTEGER PRIMARY KEY AUTOINCREMENT,
                airport_icao TEXT,
                width REAL,
                surface INTEGER,
                end1_id TEXT,
                end1_lat REAL,
                end1_lon REAL,
                end2_id TEXT,
                end2_lat REAL,
                end2_lon REAL,
                FOREIGN KEY (airport_icao) REFERENCES airports (icao)    
            )""")
        
        self.cursor.execute("""
            CREATE TABLE IF NOT EXISTS pavements (
                pavement_id INTEGER PRIMARY KEY AUTOINCREMENT,
                airport_icao TEXT,
                surface INTEGER,
                description TEXT,
                FOREIGN KEY (airport_icao) REFERENCES airports (icao)                
            )""")
        
        self.cursor.execute("""
            CREATE TABLE IF NOT EXISTS pavement_nodes (
                node_id INTEGER PRIMARY KEY AUTOINCREMENT,
                pavement_id INTEGER,
                latitude REAL,
                longitude REAL,
                node_order INTEGER,
                FOREIGN KEY (pavement_id) REFERENCES pavements (pavement_id)                
            )""")
        self.conn.commit()


    def _process_airport_batch(self, lookahead_iter: LookaheadIterator):
        """
        Private method to process airport-specific rows from apt.dat for database preparation
        """
        
        # Empty data structures
        airport_data_batch = []
        airport_database_dict = {
            'icao_code': None,
            'iata_code': None,
            'faa_code': None,
            'airport_name': None,
            'elevation': None,
            'type': None,
            'datum_lat': None,
            'datum_lon': None,
            'country': None,
            'city': None,
            'region_code': None,
            'transition_alt': None,
            'transition_level': None
        }

        # Use the iterator to parse the file and add each line to our local structure
        # Once we reach a non-airport line, put that line back
        while True:
            line = lookahead_iter.get_next_line()
            row_code = int(line.split()[0])

            if row_code in [1, 16, 17, 1302]:
                airport_data_batch.append(line)
            else:
                lookahead_iter.put_line_back(line)
                break
        
        # Loop through the batch data and make it database ready
        for line in airport_data_batch:
            parts = line.split()
            if not parts:
                break

            row_code = int(parts[0])
            if row_code != 1302:
                if row_code == 1:
                    elevation = parts[1]
                    icao = parts[4]
                    airport_name = " ".join(parts[5:])
                    type = 'Land'
                elif row_code == 16:
                    elevation = parts[1]
                    icao = parts[4]
                    airport_name = " ".join(parts[6:])
                    type = 'Sea'
                elif row_code == 17:
                    elevation = parts[1]
                    icao = parts[4]
                    airport_name = " ".join(parts[6:])
                    type = 'Heliport'

                airport_database_dict['icao_code'] = icao
                airport_database_dict['airport_name'] = airport_name
                airport_database_dict['elevation'] = int(elevation)
                airport_database_dict['type'] = type
            else:
                if parts[1] in ['city', 'country', 'datum_lat', 'datum_lon', 'faa_code', 'iata_code', 'icao_code', 'region_code', 'transition_alt', 'transition_level']:
                    if parts[1] in ['datum_lat', 'datum_lon']:
                        airport_database_dict[parts[1]] = float(parts[2])
                    elif parts[1] in ['transition_alt', 'transition_level']:
                        airport_database_dict[parts[1]] = int(parts[2])
                    else:
                        airport_database_dict[parts[1]] = parts[2]
        
        self._insert_airport_data(airport_database_dict)


    def _insert_airport_data(self, airport_database_dict):
        """
        Private method to safely insert a single airport's data into the database
        """
        try:
            # 'INSERT OR REPLACE' will update an existing record if the primary key already exists
            self.cursor.execute("""
                INSERT OR REPLACE INTO airports (icao, iata, faa, airport_name, elevation, type, latitude, longitude, country, city, region, transition_alt, transition_level)
                VALUES (:icao_code, :iata_code, :faa_code, :airport_name, :elevation, :type, :datum_lat, :datum_lon, :country, :city, :region_code, :transition_alt, :transition_level)""",
                airport_database_dict
                )
        except sqlite3.Error as e:
            print("--- DATABASE ERROR ---")
            print("Could not insert/update airport data.")
            print(f"Data that caused error: {airport_database_dict}")
            print(f"SQLite Error: {e}")
            print("----------------------")


    def update_database(self, apt_dat_path):
        """
        Parses the apt.dat file and populates the database.
        This isthe one-time import process.
        """
        # This method should be called within a 'with' block to ensure the connection is open
        if not self.conn:
            raise RuntimeError("Database connection not open. Use this method within a 'with' block.")
        
        print("Creating database tables...")
        self._create_tables()

        # We want to show the parsing progress, so we will use tqdm
        print("Pre-calculating file size for progress bar...")
        with open(apt_dat_path, 'r', encoding='utf-8') as f:
            total_lines = sum(1 for _ in f)
        
        print(f"Parsing '{apt_dat_path}' and importing to database...")

        # State variables to track context within the file
        current_airport_icao = None
        current_pavement_id = None
        is_airport_data = False
        node_order = 0

        #  Temporary data structures for proper processing and database querying
        apt_airport_metadata = {}

        with open(apt_dat_path, 'r', encoding='utf-8') as f:
            # We wrap the file object with tqdm
            # 'desc' sets a label for the bar
            # 'unit' makes the progress count more descriptive
            progress_bar = tqdm(f, total=total_lines, desc="Parsing apt.dat", unit=" lines")

            # Wrap the progress_bar iterator with out LookaheadIterator class
            lookahead_iter = LookaheadIterator(iter(progress_bar))

            # Define the initial parsing state
            parsing_state = 'NONE'

            while True:
                try:
                    # --- State: NONE ---
                    # In this state, we are just looking for what state we want to transition into
                    if parsing_state == 'NONE':
                        current_line = lookahead_iter.get_next_line()
                        row_code = int(current_line.split()[0])
                        
                        if row_code == 1 or row_code == 16 or row_code == 17 or row_code == 1302:
                            parsing_state = 'AIRPORT'
                            lookahead_iter.put_line_back(current_line)
                    
                    # --- State: AIRPORT ---
                    # In this state, we collect and process all the airport information and metadata
                    elif parsing_state == 'AIRPORT':
                        self._process_airport_batch(lookahead_iter)
                        parsing_state = 'NONE'

                except StopIteration:
                    # The iterator is empty, we have reached the end of the file
                    print("\nEnd of file reached.")
                    break
                except (ValueError, IndexError):
                    # Skip any malformed lines
                    continue

        self.conn.commit() # Commit any final transactions
        f.close()
        print("Database update complete.")

    # All other methods will be implemented here
    def get_nearest_airport(self, lat, lon, radius_deg=1.0):
        """
        Finds the single closest airport to a given coordinate.

        Returns:
            A dictionary with airport data (icao, name, lat, lon) or None
        """
        lat_min, lat_max = lat - radius_deg, lat + radius_deg
        lon_min, lon_max = lon - radius_deg, lon + radius_deg

        self.cursor.execute("""
            SELECT icao, name, latitude, longitude FROM airports
            WHERE latitude BETWEEN ? AND ? AND longitude BETWEEN ? AND ?""",
            (lat_min, lat_max, lon_min, lon_max))
        
        candidate_airports = self.cursor.fetchall()

        closest_airport = None
        min_dist_sq = float('inf')

        for airport in candidate_airports:
            dist_sq = (airport['latitude'] - lat)**2 + (airport['longitude'] - lon)**2
            if dist_sq < min_dist_sq:
                min_dist_sq = dist_sq
                closest_airport = dict(airport) # Convert from sqlite3.Row to dict

        return closest_airport
    

    def get_airport(self, icao):
        """
        Returns a single dictionary for the given ICAO
        """
        self.cursor.execute("SELECT * FROM airports WHERE icao = ?", (icao,))
        airport_row = self.cursor.fetchone()
        if airport_row:
            return dict(airport_row)
        else:
            return None


    def get_runways(self, icao):
        """
        Returns a list of runway data dictionaries for a given airport.
        """
        self.cursor.execute("SELECT * FROM runways WHERE airport_icao = ?", (icao,))
        return [dict(row) for row in self.cursor.fetchall()]
    

    def get_pavements(self, icao):
        """
        Returns a list of pavement dictionaries, each with a list of nodes.
        """
        self.cursor.execute("SELECT * FROM pavements WHERE airport_icao = ?", (icao,))
        pavements = self.cursor.fetchall()

        result = []
        for pav in pavements:
            pavement_data = dict(pav)
            self.cursor.execute("""
                SELECT latitude, longitude, FROM pavement_nodes
                WHERE pavement_id = ? ORDER BY node_order""",
                (pavement_data['pavement_id'],))
            pavement_data['nodes'] = [(row['latitude'], row['longitude']) for row in self.cursor.fetchall()]
            result.append(pavement_data)

        return result
    
