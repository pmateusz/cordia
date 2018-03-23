IF OBJECT_ID (N'dbo.CalculateDuration', N'FN') IS NOT NULL
    DROP FUNCTION CalculateDuration;
GO

CREATE FUNCTION dbo.CalculateDuration(@checkin datetime, @checkout datetime)
RETURNS int
AS
BEGIN
    DECLARE @duration int;
	DECLARE @day_duration int;
	DECLARE @checkin_date date;
	DECLARE @checkout_date date;
    IF @checkin <= @checkout BEGIN
		SET @checkin_date = CONVERT(date, @checkin);
		SET @checkout_date = CONVERT(date, @checkout)
		IF @checkin_date = @checkout_date BEGIN
			RETURN DATEDIFF(second, CONVERT(time, @checkin), CONVERT(time, @checkout));
		END

		SET @day_duration = DATEDIFF(day,  @checkin_date, @checkout_date);
		IF @day_duration >= 1 BEGIN
			RETURN (@day_duration - 1) * 24 * 3600
				+ DATEDIFF(second, CONVERT(time, @checkin), CONVERT(time, '23:59:59'))
				+ DATEDIFF(second, CONVERT(time, '00:00:00'), CONVERT(time, @checkout)) + 1;
		END
	END ELSE BEGIN
		RETURN -1 * dbo.CalculateDuration(@checkout, @checkin)
	END
    RETURN @duration;
END;
GO