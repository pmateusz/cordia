"""Point of interest at the GPS location"""


class PointOfInterest:  # pylint: disable=too-few-public-methods
    """Point of interest at the GPS location"""

    class Tag:
        """Category of the point of interest"""

        def __init__(self, key=None, label=None, domain=None):
            self.__key = key
            self.__label = label
            self.__domain = domain

        def __eq__(self, other):
            if not isinstance(other, PointOfInterest.Tag):
                return False

            return self.__key == other.key and self.__label == other.label and self.__domain == other.domain

        def __hash__(self):
            return hash(self.tuple())

        def __str__(self):
            return self.tuple().__str__()

        def __repr__(self):
            return self.tuple().__repr__()

        def tuple(self):
            """Returns object as tuple"""

            return self.__key, self.__label, self.__domain

        @property
        def domain(self):
            """Returns a property"""

            return self.__domain

        @property
        def label(self):
            """Returns a property"""

            return self.__label

        @property
        def key(self):
            """Returns a property"""

            return self.__key

    def __init__(self, bundle):
        self.__tags = []

        # handle standard tag
        key, label, domain = bundle.get('place_id', None), bundle.get('class', None), bundle.get('type', None)
        if key or label or domain:
            self.__tags.append(PointOfInterest.Tag(key=key, label=label, domain=domain))

        key, label = bundle.get('osm_id', None), bundle.get('osm_type', None)
        # handle Open Street Map tag
        if key or label:
            self.__tags.append(PointOfInterest.Tag(key=key, label=label))

    @property
    def tags(self):
        """Get a property"""

        return self.__tags